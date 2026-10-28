#include "app/cli.h"
#include "test.h"

using namespace sm;

static ParseResult P(std::vector<std::string> a) { return parseCli(a); }

TEST(cli_default_is_summary) {
  auto r = P({});
  CHECK(r.ok);
  CHECK_EQ(r.opts.command, std::string("summary"));
  CHECK(r.opts.format == Format::Text);
}

TEST(cli_flags_anywhere_and_inline_values) {
  auto r = P({"--json", "processes", "--sort=memory", "--top", "10", "--interval", "250"});
  CHECK(r.ok);
  CHECK_EQ(r.opts.command, std::string("processes"));
  CHECK(r.opts.format == Format::Json);
  CHECK(r.opts.sort == ProcessSort::Memory);
  CHECK_EQ(*r.opts.top, 10u);
  CHECK_EQ(*r.opts.intervalMs, 250u);
}

TEST(cli_process_pid) {
  auto r = P({"process", "1234"});
  CHECK(r.ok);
  CHECK_EQ(*r.opts.pid, 1234u);
  CHECK(!P({"process"}).ok);
  CHECK(!P({"process", "12a"}).ok);
  CHECK(!P({"process", "-5"}).ok);
  CHECK(!P({"process", "1", "2"}).ok);
  CHECK(!P({"process", "99999999999"}).ok);
}

TEST(cli_rejects_bad_values) {
  CHECK(!P({"--interval", "nope"}).ok);
  CHECK(!P({"--interval", "50"}).ok);        // below minimum
  CHECK(!P({"--interval", "3600001"}).ok);   // above maximum
  CHECK(!P({"--interval"}).ok);              // missing value
  CHECK(!P({"processes", "--top", "0"}).ok);
  CHECK(!P({"processes", "--sort", "size"}).ok);
  CHECK(!P({"--json", "--csv"}).ok);
  CHECK(!P({"bogus"}).ok);
  CHECK(!P({"--bogus"}).ok);
  CHECK(!P({"cpu", "extra"}).ok);
  CHECK(!P({"--json=1"}).ok);  // flag does not take a value
}

TEST(cli_command_specific_validation) {
  CHECK(!P({"cpu", "--count", "3"}).ok);
  CHECK(P({"export", "--count", "3"}).ok);
  CHECK(P({"watch", "--duration", "10"}).ok);
  CHECK(!P({"export", "--count", "3", "--duration", "2"}).ok);
  CHECK(!P({"cpu", "--sort", "cpu"}).ok);
  CHECK(!P({"--append"}).ok);
  CHECK(P({"export", "--append", "--output", "x.csv"}).ok);
  CHECK(!P({"watch", "--output", "x.txt"}).ok);
  CHECK(P({"watch", "--csv", "--output", "x.csv"}).ok);
}

TEST(cli_help_and_version) {
  CHECK(P({"--help"}).opts.help);
  CHECK(P({"/?"}).opts.help);
  CHECK(P({"--version"}).opts.version);
  auto h = P({"help", "cpu"});
  CHECK(h.ok);
  CHECK(!P({"help", "nothing"}).ok);
  CHECK(!commandHelp("watch").empty());
  CHECK(commandHelp("nothing").empty());
  CHECK(usageText().find("sevenmon") != std::string::npos);
}

TEST(cli_unicode_paths_preserved) {
  std::string path = "C:\\Daten\\M\xC3\xBCller\\stats file.csv";
  auto r = P({"export", "--output", path});
  CHECK(r.ok);
  CHECK_EQ(r.opts.output, path);
  CHECK_EQ(P({"export", "--output=" + path}).opts.output, path);
}

TEST(cli_case_insensitive_command) { CHECK_EQ(P({"CPU"}).opts.command, std::string("cpu")); }
