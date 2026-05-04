#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [[ "${VLON_FLOW_IN_DOCKER:-0}" != "1" ]]; then
  BUILD_CONTAINER=${BUILD_CONTAINER:-fuwal-testbuild}

  if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required when running this script from the host" >&2
    exit 1
  fi

  if ! docker inspect "${BUILD_CONTAINER}" >/dev/null 2>&1; then
    echo "Docker container not found: ${BUILD_CONTAINER}" >&2
    exit 1
  fi

  if [[ "$(docker inspect -f '{{.State.Running}}' "${BUILD_CONTAINER}")" != "true" ]]; then
    docker start "${BUILD_CONTAINER}" >/dev/null
  fi

  exec docker exec \
    -e VLON_FLOW_IN_DOCKER=1 \
    -e CHAIN_URL="${CHAIN_URL:-}" \
    -e WALLET_NAME="${WALLET_NAME:-}" \
    -e WALLET_PASSWORD_FILE="${WALLET_PASSWORD_FILE:-}" \
    -e TEST_ACCOUNT="${TEST_ACCOUNT:-}" \
    -e TEST_PUBKEY="${TEST_PUBKEY:-}" \
    -e STAKE_QUANTITY="${STAKE_QUANTITY:-}" \
    -e VLON_MAX_SUPPLY="${VLON_MAX_SUPPLY:-}" \
    -e TEST_FUND="${TEST_FUND:-}" \
    -e BUILD="${BUILD:-}" \
    -e DEPLOY_TOKEN="${DEPLOY_TOKEN:-}" \
    -e DEPLOY_SYSTEM="${DEPLOY_SYSTEM:-}" \
    -e GRANT_CODE="${GRANT_CODE:-}" \
    -e CONFIG_ELECTION="${CONFIG_ELECTION:-}" \
    -e RUN_SUBVOTE="${RUN_SUBVOTE:-}" \
    -w "${SCRIPT_DIR}" \
    "${BUILD_CONTAINER}" \
    /bin/bash ./test_vlon_vote_flow.sh "$@"
fi

: "${CHAIN_URL:=https://t.flonscan.io}"
: "${WALLET_NAME:=flontest}"
: "${WALLET_PASSWORD_FILE:=/root/.password.txt}"
: "${TEST_PUBKEY:=FU6Dm6xR3JxpeEhdswTV4qTawYXjBcV4gtWjRPELaS9wbQzNmSUC}"
: "${STAKE_QUANTITY:=1.00000000 FLON}"
: "${VLON_MAX_SUPPLY:=10000000000.00000000 VLON}"
: "${TEST_FUND:=5.00000000 FLON}"
: "${BUILD:=0}"
: "${DEPLOY_TOKEN:=0}"
: "${DEPLOY_SYSTEM:=1}"
: "${GRANT_CODE:=1}"
: "${CONFIG_ELECTION:=1}"
: "${RUN_SUBVOTE:=1}"

if [[ -z "${TEST_ACCOUNT:-}" ]]; then
  chars=(a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5)
  suffix=""
  for _ in 1 2 3 4; do
    suffix+="${chars[$((RANDOM % ${#chars[@]}))]}"
  done
  TEST_ACCOUNT="vlonflow${suffix}"
fi

MODE=${1:-flow}

SYSTEM_BUILD_DIR="${SCRIPT_DIR}/build/contracts/flon.system"
TOKEN_BUILD_DIR="${SCRIPT_DIR}/build/contracts/flon.token"
SYSTEM_WASM="${SYSTEM_BUILD_DIR}/flon.system.wasm"
SYSTEM_ABI="${SYSTEM_BUILD_DIR}/flon.system.abi"
TOKEN_WASM="${TOKEN_BUILD_DIR}/flon.token.wasm"
TOKEN_ABI="${TOKEN_BUILD_DIR}/flon.token.abi"

log() {
  printf '\n==> %s\n' "$*"
}

run() {
  printf '+'
  printf ' %q' "$@"
  printf '\n'
  "$@"
}

run_checked() {
  local output
  local status

  printf '+'
  printf ' %q' "$@"
  printf '\n'

  set +e
  output=$("$@" 2>&1)
  status=$?
  set -e

  printf '%s\n' "${output}"

  if (( status != 0 )); then
    return "${status}"
  fi

  if printf '%s\n' "${output}" | grep -Eq 'failed transaction|transaction_exception|soft_except|Error [0-9]{7}|ERROR:'; then
    return 1
  fi

  return 0
}

cli() {
  fucli -u "${CHAIN_URL}" "$@"
}

push_action() {
  cli push action "$@"
}

push_action_checked() {
  run_checked cli push action "$@"
}

usage() {
  cat <<'USAGE'
Usage:
  ./test_vlon_vote_flow.sh [flow|create-vlon|deploy|help]

Default mode is "flow".

Environment overrides:
  CHAIN_URL              default: https://t.flonscan.io
  BUILD_CONTAINER        default: fuwal-testbuild, used only from host
  TEST_ACCOUNT           default: random vlonflowXXXX account
  STAKE_QUANTITY         default: 1.00000000 FLON
  VLON_MAX_SUPPLY        default: 10000000000.00000000 VLON
  BUILD=1               rebuild flon.system/flon.token before running
  DEPLOY_SYSTEM=0        skip deploying local flon.system to flon
  DEPLOY_TOKEN=1         deploy local flon.token to flon.token
  RUN_SUBVOTE=0          skip subvote/retire check
USAGE
}

asset_units() {
  local asset_text="$1"
  local amount="${asset_text%% *}"

  if [[ -z "${amount}" || "${amount}" == "${asset_text}" ]]; then
    amount="0.00000000"
  fi

  local whole="${amount%%.*}"
  local frac="0"
  if [[ "${amount}" == *.* ]]; then
    frac="${amount#*.}"
  fi
  frac="${frac}00000000"
  frac="${frac:0:8}"

  echo $((10#${whole} * 100000000 + 10#${frac}))
}

balance_of() {
  local account="$1"
  local symbol="$2"
  local balance
  balance=$(cli get currency balance flon.token "${account}" "${symbol}" | tail -n 1 || true)
  if [[ -z "${balance}" ]]; then
    printf '0.00000000 %s\n' "${symbol}"
  else
    printf '%s\n' "${balance}"
  fi
}

assert_units_delta() {
  local label="$1"
  local before="$2"
  local after="$3"
  local expected_delta="$4"
  local before_units
  local after_units

  before_units=$(asset_units "${before}")
  after_units=$(asset_units "${after}")

  if (( after_units - before_units != expected_delta )); then
    echo "ASSERT FAILED: ${label}" >&2
    echo "  before: ${before}" >&2
    echo "  after:  ${after}" >&2
    echo "  expected delta units: ${expected_delta}" >&2
    echo "  actual delta units:   $((after_units - before_units))" >&2
    exit 1
  fi
}

assert_units_delta_lte() {
  local label="$1"
  local before="$2"
  local after="$3"
  local max_delta="$4"
  local before_units
  local after_units
  local actual_delta

  before_units=$(asset_units "${before}")
  after_units=$(asset_units "${after}")
  actual_delta=$((after_units - before_units))

  if (( actual_delta > max_delta )); then
    echo "ASSERT FAILED: ${label}" >&2
    echo "  before: ${before}" >&2
    echo "  after:  ${after}" >&2
    echo "  maximum allowed delta units: ${max_delta}" >&2
    echo "  actual delta units:          ${actual_delta}" >&2
    exit 1
  fi
}

unlock_wallet() {
  if [[ -f "${WALLET_PASSWORD_FILE}" ]]; then
    log "Unlock wallet ${WALLET_NAME}"
    cli wallet unlock -n "${WALLET_NAME}" --password "$(cat "${WALLET_PASSWORD_FILE}")" >/dev/null 2>&1 || true
  fi
}

ensure_build_outputs() {
  if [[ "${BUILD}" == "1" || ! -f "${SYSTEM_WASM}" || ! -f "${SYSTEM_ABI}" || ! -f "${TOKEN_WASM}" || ! -f "${TOKEN_ABI}" ]]; then
    log "Build flon.system and flon.token"
    run mkdir -p "${SCRIPT_DIR}/build"
    run cmake -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" -DBUILD_TESTS=false -DCMAKE_INSTALL_PREFIX="${SCRIPT_DIR}/build/install"
    run make -C "${SCRIPT_DIR}/build/contracts" -j1 flon.system flon.token
  fi
}

deploy_contracts() {
  ensure_build_outputs

  if [[ "${DEPLOY_TOKEN}" == "1" ]]; then
    log "Deploy flon.token"
    run cli set contract flon.token "${TOKEN_BUILD_DIR}" flon.token.wasm flon.token.abi -p flon.token@active
  fi

  if [[ "${DEPLOY_SYSTEM}" == "1" ]]; then
    log "Deploy flon.system"
    run cli set contract flon "${SYSTEM_BUILD_DIR}" flon.system.wasm flon.system.abi -p flon@active
  fi
}

grant_code_permissions() {
  if [[ "${GRANT_CODE}" != "1" ]]; then
    return
  fi

  log "Grant code permissions used by inline actions"
  grant_code_permission flon
  grant_code_permission flon.vote
}

grant_code_permission() {
  local account="$1"
  local code_perm="${account}@flon.code"

  if cli get account "${account}" | grep -q "${code_perm}"; then
    echo "${account} already has ${code_perm}"
    return
  fi

  run cli set account permission "${account}" active --add-code -p "${account}@active"
}

create_vlon() {
  log "Check VLON stats"
  local stats
  stats=$(cli get currency stats flon.token VLON || true)
  printf '%s\n' "${stats}"

  if printf '%s\n' "${stats}" | grep -q '"VLON"'; then
    if ! printf '%s\n' "${stats}" | grep -q '"issuer": "flon"'; then
      echo "VLON already exists, but issuer is not flon" >&2
      exit 1
    fi
    if ! printf '%s\n' "${stats}" | grep -Eq '"supply": "[0-9]+\.[0-9]{8} VLON"'; then
      echo "VLON already exists, but precision is not 8" >&2
      exit 1
    fi
    log "VLON already exists on flon.token"
    return
  fi

  log "Create VLON on flon.token with issuer flon"
  push_action_checked flon.token create "[\"flon\",\"${VLON_MAX_SUPPLY}\"]" -p flon.token@active
}

account_exists() {
  cli get account "$1" >/dev/null 2>&1
}

ensure_test_account() {
  if account_exists "${TEST_ACCOUNT}"; then
    log "Test account exists: ${TEST_ACCOUNT}"
    return
  fi

  log "Create test account ${TEST_ACCOUNT}"
  run cli system newaccount flon "${TEST_ACCOUNT}" "${TEST_PUBKEY}" --fund-account "${TEST_FUND}" -p flon@active
}

fund_test_account() {
  log "Fund ${TEST_ACCOUNT}"
  run cli transfer flon "${TEST_ACCOUNT}" "${TEST_FUND}" "vlon vote flow test" -p flon@active
}

configure_election_if_needed() {
  if [[ "${CONFIG_ELECTION}" != "1" ]]; then
    return
  fi

  local global
  global=$(cli get table flon flon global)
  if ! printf '%s\n' "${global}" | grep -q '"election_activated_time": "1970-01-01T00:00:00.000"'; then
    log "Election is already configured"
    return
  fi

  local activate_at
  local reward_at
  activate_at=$(date -u -d '+3 seconds' '+%Y-%m-%dT%H:%M:%S.000' 2>/dev/null || date -u -v+3S '+%Y-%m-%dT%H:%M:%S.000')
  reward_at=$(date -u -d '+4 seconds' '+%Y-%m-%dT%H:%M:%S.000' 2>/dev/null || date -u -v+4S '+%Y-%m-%dT%H:%M:%S.000')

  log "Configure election for subvote test"
  push_action_checked flon cfgelection "[\"${activate_at}\",\"${reward_at}\",\"0.00000000 FLON\"]" -p flon@active
  sleep 5
}

show_balances() {
  log "Balances for ${TEST_ACCOUNT}"
  cli get currency balance flon.token "${TEST_ACCOUNT}" FLON || true
  cli get currency balance flon.token "${TEST_ACCOUNT}" VLON || true
}

run_flow() {
  unlock_wallet
  deploy_contracts
  grant_code_permissions
  create_vlon
  ensure_test_account
  fund_test_account
  configure_election_if_needed

  local stake_units
  local flon_before
  local flon_after_add
  local vlon_before
  local vlon_after_add
  local vlon_after_sub
  stake_units=$(asset_units "${STAKE_QUANTITY}")

  show_balances
  flon_before=$(balance_of "${TEST_ACCOUNT}" FLON)
  vlon_before=$(balance_of "${TEST_ACCOUNT}" VLON)

  log "Run flon::addvote"
  push_action_checked flon addvote "[\"${TEST_ACCOUNT}\",\"${STAKE_QUANTITY}\"]" -p "${TEST_ACCOUNT}@active"

  flon_after_add=$(balance_of "${TEST_ACCOUNT}" FLON)
  vlon_after_add=$(balance_of "${TEST_ACCOUNT}" VLON)
  assert_units_delta_lte "FLON should decrease by at least the staked amount after addvote" "${flon_before}" "${flon_after_add}" "-${stake_units}"
  assert_units_delta "VLON should increase after addvote" "${vlon_before}" "${vlon_after_add}" "${stake_units}"

  log "After addvote"
  show_balances
  cli get table flon flon voters -l 5 || true

  if [[ "${RUN_SUBVOTE}" != "1" ]]; then
    return
  fi

  log "Run flon::subvote"
  if ! push_action_checked flon subvote "[\"${TEST_ACCOUNT}\",\"${STAKE_QUANTITY}\"]" -p "${TEST_ACCOUNT}@active"; then
    echo "subvote failed" >&2
    exit 1
  fi

  vlon_after_sub=$(balance_of "${TEST_ACCOUNT}" VLON)
  assert_units_delta "VLON should return to previous balance after subvote" "${vlon_before}" "${vlon_after_sub}" "0"

  log "After subvote"
  show_balances
  cli get table flon "${TEST_ACCOUNT}" voterefund || true
}

case "${MODE}" in
  flow)
    run_flow
    ;;
  create-vlon)
    unlock_wallet
    deploy_contracts
    create_vlon
    ;;
  deploy)
    unlock_wallet
    deploy_contracts
    grant_code_permissions
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "Unknown mode: ${MODE}" >&2
    usage >&2
    exit 1
    ;;
esac

log "Done"
