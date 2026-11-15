# Reproducer for the ChatJoinResult abort

Out-of-tree test for the fix in tdlight-team/tdlight-telegram-bot-api#97. The repository has no tests for `Client.cpp`, so this is a harness and not a proposal to add test infrastructure.

On the commit before the fix (`d1e9441`, 10.4) all 8 cases abort with `SIGABRT` on `CHECK(result->get_id() == td_api::ok::ID)` (`joinChat`) or `CHECK(result->get_id() == td_api::chat::ID)` (`joinChatByInviteLink`). With the fix, all 8 pass.

## How it works

It calls the real `Client::process_join_chat_query` with `chat_id` or `invite_link`, takes the TDLib request that the client would send, and answers it with the real `Client::on_result` and a hand-built `td_api::chatJoinResult*` object. Only the transport to TDLib is replaced: `CMakeLists.txt` generates a copy of `Client.cpp` in the build directory where `Client::do_send_request` hands the request to the test instead of a `td::ClientActor` (configuration fails if that function changes). The test accesses private members with `#define private public` around `Client.h`.

The cases are `chat_id` and `invite_link`, each with `success`, `request_sent`, `guard_bot_approval_required` and `declined`.

## Running it

```
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTELEGRAM_BOT_API_BUILD_TESTS=ON
cmake --build build --target test-join-chat-result
ctest --test-dir build -R join-chat-result --output-on-failure
```

Use `-R join-chat-result`: the TDLib subproject registers its own `run_all_tests`, which is not built here. A single case: `build/test-join-chat-result chat_id-success`.

Building TDLib needs several GB of RAM per compilation unit. `test/test.nix` builds and runs every case on an x86_64-linux Nix builder, first with the fix reverted (`test/fix.patch`) and then with it applied:

```
nix build --impure --expr 'import ./test/test.nix { src = ./.; fix = ./test/fix.patch; }'
```

## Recorded result (v10.2 sources, the same code as here)

With the fix reverted, `chat_id-*` cases fail at `Client.cpp:8971` and `invite_link-*` cases at `:8992`, exit status 134. With the fix: success returns the chat, `request_sent` returns `400 Bad Request: INVITE_REQUEST_SENT`, `guard_bot_approval_required` and `declined` return `403 Forbidden`, and `ctest` reports `100% tests passed out of 8`.

Limit: the success case runs with the chat already in the client's cache. The invite link path relies on TDLib sending `updateNewChat` before the result, as the old callback did.
