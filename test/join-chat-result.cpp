//
// Checks how the user-mode joinChat query handles every ChatJoinResult that TDLib can answer with.
//
// The real Client::process_join_chat_query and the real Client::on_result are used. Only the transport to TDLib is
// replaced: CMakeLists.txt generates a copy of Client.cpp in which Client::do_send_request records the request
// instead of sending it to a td::ClientActor. The recorded request is then answered by hand with a td_api object.
//
// Usage: test-join-chat-result [case]. Without arguments all cases are run, one after another.
//
// Cases of the form <method>-<result> call process_join_chat_query with chat_id (method "chat_id") or with
// invite_link (method "invite_link") and answer the resulting TDLib request with chatJoinResultSuccess,
// chatJoinResultRequestSent, chatJoinResultGuardBotApprovalRequired or chatJoinResultDeclined.

// everything included by Client.h is included beforehand, so that only Client.h itself is affected by the define
#include "telegram-bot-api/Query.h"
#include "telegram-bot-api/Stats.h"
#include "telegram-bot-api/WebhookActor.h"

#include "td/telegram/ClientActor.h"
#include "td/telegram/td_api.h"

#include "td/net/HttpFile.h"

#include "td/actor/actor.h"
#include "td/actor/SignalSlot.h"

#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

// the tested callbacks and handlers are private members of Client
#define private public
#include "telegram-bot-api/Client.h"
#undef private

namespace telegram_bot_api {

static td::vector<std::pair<td::uint64, td::td_api::object_ptr<td::td_api::Function>>> sent_requests;

// called instead of sending the request to TDLib by the generated copy of Client.cpp
void test_record_request(td::uint64 id, td::td_api::object_ptr<td::td_api::Function> request) {
  sent_requests.emplace_back(id, std::move(request));
}

}  // namespace telegram_bot_api

#include "Client.test.cpp"

namespace telegram_bot_api {

namespace {

constexpr td::int64 CHAT_ID = -4242;
constexpr td::int64 GROUP_ID = 4242;

void expect(bool condition, const char *description) {
  if (!condition) {
    std::fprintf(stderr, "EXPECTATION FAILED: %s\n", description);
    std::exit(1);
  }
}

struct Case {
  const char *name;
  bool use_invite_link;
  td::td_api::object_ptr<td::td_api::Object> (*make_result)();
  int expected_status;
  const char *expected_text;
};

td::td_api::object_ptr<td::td_api::Object> make_success() {
  return td::td_api::make_object<td::td_api::chatJoinResultSuccess>(CHAT_ID);
}

td::td_api::object_ptr<td::td_api::Object> make_request_sent() {
  return td::td_api::make_object<td::td_api::chatJoinResultRequestSent>();
}

td::td_api::object_ptr<td::td_api::Object> make_guard_bot_approval_required() {
  return td::td_api::make_object<td::td_api::chatJoinResultGuardBotApprovalRequired>(1234, 5678);
}

td::td_api::object_ptr<td::td_api::Object> make_declined() {
  return td::td_api::make_object<td::td_api::chatJoinResultDeclined>();
}

const Case CASES[] = {
    {"chat_id-success", false, make_success, 200, "\"id\":-4242"},
    {"invite_link-success", true, make_success, 200, "\"id\":-4242"},
    {"chat_id-request_sent", false, make_request_sent, 400, "INVITE_REQUEST_SENT"},
    {"invite_link-request_sent", true, make_request_sent, 400, "INVITE_REQUEST_SENT"},
    {"chat_id-guard_bot_approval_required", false, make_guard_bot_approval_required, 403, "Forbidden"},
    {"invite_link-guard_bot_approval_required", true, make_guard_bot_approval_required, 403, "Forbidden"},
    {"chat_id-declined", false, make_declined, 403, "Forbidden"},
    {"invite_link-declined", true, make_declined, 403, "Forbidden"},
};

void run_case(const Case &test_case) {
  std::fprintf(stderr, "case %s\n", test_case.name);
  sent_requests.clear();

  // never destroyed: ~Client needs a running scheduler
  auto *client = new Client(td::ActorShared<>(), "", true, false, 0, nullptr, td::ActorId<BotStatActor>());
  auto *group_info = client->add_group_info(GROUP_ID);
  group_info->is_active = true;
  auto *chat_info = client->add_chat(CHAT_ID);
  chat_info->type = Client::ChatInfo::Type::Group;
  chat_info->title = "test group";
  chat_info->group_id = GROUP_ID;
  chat_info->accent_color_id = 0;
  chat_info->permissions = td::td_api::make_object<td::td_api::chatPermissions>();

  // the query only points to its arguments
  static td::vector<std::unique_ptr<std::string>> strings;
  auto make_slice = [&](const char *value) {
    strings.push_back(std::make_unique<std::string>(value));
    return td::MutableSlice(*strings.back());
  };
  td::vector<std::pair<td::MutableSlice, td::MutableSlice>> args;
  if (test_case.use_invite_link) {
    args.emplace_back(make_slice("invite_link"), make_slice("https://t.me/+AbCdEfGhIjKl"));
  } else {
    args.emplace_back(make_slice("chat_id"), make_slice("-4242"));
  }
  auto query = td::make_unique<Query>(td::vector<td::BufferSlice>(), td::Slice(), true, false, make_slice("joinchat"),
                                      std::move(args), td::vector<std::pair<td::MutableSlice, td::MutableSlice>>(),
                                      td::vector<td::HttpFile>(), nullptr, td::IPAddress(), true);

  td::unique_ptr<Query> answered_query;
  auto promise = td::PromiseCreator::lambda(
      [&](td::Result<td::unique_ptr<Query>> r_query) { answered_query = r_query.move_as_ok(); });
  PromisedQueryPtr promised_query(query.release(), PromiseDeleter(std::move(promise)));

  auto status = client->process_join_chat_query(promised_query);
  expect(status.is_ok(), "process_join_chat_query returned an error");
  expect(sent_requests.size() == 1, "exactly one request must be sent to TDLib");
  auto expected_request_id = test_case.use_invite_link ? td::td_api::joinChatByInviteLink::ID : td::td_api::joinChat::ID;
  expect(sent_requests[0].second->get_id() == expected_request_id, "unexpected TDLib request");
  expect(answered_query == nullptr, "the query must stay pending until TDLib answers");

  // aborts on the CHECK in the callback if it doesn't know the answer
  client->on_result(sent_requests[0].first, test_case.make_result());

  expect(answered_query != nullptr, "the query must be answered");
  auto answer = answered_query->answer().as_slice().str();
  std::fprintf(stderr, "  status %d, answer %s\n", answered_query->http_status_code(), answer.c_str());
  expect(answered_query->http_status_code() == test_case.expected_status, "unexpected HTTP status");
  expect(answer.find(test_case.expected_text) != std::string::npos, "unexpected answer");
}

}  // namespace

}  // namespace telegram_bot_api

int main(int argc, char **argv) {
  using telegram_bot_api::CASES;
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  bool found = false;
  for (const auto &test_case : CASES) {
    if (argc > 1 && std::string(argv[1]) != test_case.name) {
      continue;
    }
    found = true;
    telegram_bot_api::run_case(test_case);
  }
  if (!found) {
    std::fprintf(stderr, "unknown case %s\n", argv[1]);
    return 2;
  }
  std::fprintf(stderr, "OK\n");
  return 0;
}
