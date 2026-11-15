# Builds the test target and runs every case, recording the exit status and the output of each one.
#
#   nix build --impure --expr 'import ./test/test.nix { src = ./.; fix = ./test/fix.patch; }'
#
# src is a full checkout of this branch (including the td submodule), which already has the fix applied.
# The cases run first with the fix reverted (fix.patch) and then again with it applied, so that TDLib is only
# compiled once. Without fix, the cases run against src as is.
{ src, fix ? null, system ? "x86_64-linux" }:
let
  pkgs = import <nixpkgs> { inherit system; };
in
pkgs.stdenv.mkDerivation {
  pname = "tdlight-telegram-bot-api-join-chat-test";
  version = "10.2";
  inherit src;
  nativeBuildInputs = [ pkgs.cmake pkgs.gperf pkgs.patch ];
  buildInputs = [ pkgs.openssl pkgs.zlib ];
  cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" "-DTELEGRAM_BOT_API_BUILD_TESTS=ON" ];
  enableParallelBuilding = true;
  dontInstall = true;
  postUnpack = pkgs.lib.optionalString (fix != null) ''
    # start from the tree without the fix
    patch -R -p1 -d "$sourceRoot" < ${fix}
  '';
  buildPhase = ''
    runHook preBuild
    cmake --build . --target test-join-chat-result -j$NIX_BUILD_CORES
    runHook postBuild
  '';
  doCheck = true;
  checkPhase = ''
    run_cases() {
      local stage=$1
      for c in chat_id-success invite_link-success chat_id-request_sent invite_link-request_sent \
               chat_id-guard_bot_approval_required invite_link-guard_bot_approval_required \
               chat_id-declined invite_link-declined; do
        set +e
        ./test-join-chat-result $c > case.out 2>&1
        rc=$?
        set -e
        echo "[$stage] $c: exit status $rc"
        sed "s/^/    /" case.out
      done
    }
    {
      echo "=== ${if fix == null then "tree as is" else "fix reverted"} ==="
      run_cases before
      ${pkgs.lib.optionalString (fix != null) ''
        echo "=== fix applied ==="
        (cd .. && patch -p1 < ${fix})
        cmake --build . --target test-join-chat-result -j$NIX_BUILD_CORES > /dev/null
        run_cases after
        echo "=== ctest ==="
        ctest -R join-chat-result --output-on-failure
      ''}
    } 2>&1 | tee test-report.txt
    mkdir -p $out
    cp test-report.txt $out/
  '';
}
