/*
 * SonicWave Launcher \u2014 interface TUI (FTXUI) pour piloter le projet
 * BE_POO.
 *
 * Emplacement prévu :  BE_POO/sonicwave-launcher/src/main.cpp
 *
 * ---- \u00c0 copier dans BE_POO/sonicwave-launcher/CMakeLists.txt
 * ------------------
 *
 *   cmake_minimum_required(VERSION 3.22)
 *   project(sonicwave-launcher LANGUAGES CXX)
 *
 *   set(CMAKE_CXX_STANDARD 17)
 *   set(CMAKE_CXX_STANDARD_REQUIRED ON)
 *   set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
 *
 *   include(FetchContent)
 *   set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
 *   FetchContent_Declare(ftxui
 *     GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
 *     GIT_TAG        v5.0.0
 *     GIT_SHALLOW    TRUE)
 *   FetchContent_MakeAvailable(ftxui)
 *
 *   find_package(Threads REQUIRED)
 *
 *   add_executable(sonicwave-launcher src/main.cpp)
 *   target_link_libraries(sonicwave-launcher PRIVATE
 *     ftxui::screen ftxui::dom ftxui::component Threads::Threads)
 *
 * ------------------------------------------------------------------------------
 *
 * Compilation (FTXUI est téléchargé automatiquement au premier configure) :
 *   sudo apt install build-essential cmake git
 *   cd sonicwave-launcher
 *   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build -j
 *
 * Utilisation :
 *   ./build/sonicwave-launcher     -> interface TUI
 *   ./build/sonicwave-launcher esp upload --port /dev/ttyUSB0
 *   ./build/sonicwave-launcher studio run -- --mon-option
 *   ./build/sonicwave-launcher doctor
 *   ./build/sonicwave-launcher --help
 *
 * Raccourcis TUI :
 *   Entrée : lancer l'action   Tab : changer de zone   \u00c9chap : annuler la
 * tâche PgPréc/PgSuiv/molette : défiler le journal   Fin : revenir en bas r :
 * rescanner les ports   l : effacer le journal   q : quitter
 *
 * La racine du projet est trouvée en remontant depuis le dossier courant ou
 * depuis l'exécutable (dossier contenant sonicwave-control et
 * sonicwave-studio). On peut aussi la forcer avec la variable d'environnement
 * SONICWAVE_ROOT.
 */

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace fs = std::filesystem;
using namespace ftxui;

// ============================================================================
// Configuration \u2014 à adapter à ton projet
// ============================================================================
constexpr const char *STUDIO_EXE = "sw-studio"; // nom donné à add_executable()
constexpr const char *PIO_ENV = "nodemcuv2";    // env de platformio.ini
constexpr const char *BUILD_TYPE = "Debug";     // build type CMake du studio
constexpr size_t MAX_LOG_LINES = 5000;

// ============================================================================
// Petits utilitaires
// ============================================================================
struct Paths {
  fs::path root, esp, studio, studio_build;
};

static bool looks_like_root(const fs::path &p) {
  std::error_code ec;
  return fs::is_directory(p / "sonicwave-control", ec) &&
         fs::is_directory(p / "sonicwave-studio", ec);
}

static std::optional<fs::path> find_root() {
  std::vector<fs::path> starts;
  if (const char *env = std::getenv("SONICWAVE_ROOT"))
    starts.emplace_back(env);
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (!ec)
    starts.push_back(cwd);
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec)
    starts.push_back(exe.parent_path());

  for (fs::path p : starts) {
    p = fs::weakly_canonical(p, ec);
    if (ec || p.empty())
      continue;
    while (true) {
      if (looks_like_root(p))
        return p;
      if (p == p.root_path() || !p.has_parent_path())
        break;
      p = p.parent_path();
    }
  }
  return std::nullopt;
}

static std::optional<fs::path> find_in_path(const std::string &name) {
  const char *path = std::getenv("PATH");
  if (!path)
    return std::nullopt;
  std::stringstream ss(path);
  std::string dir;
  while (std::getline(ss, dir, ':')) {
    if (dir.empty())
      continue;
    fs::path candidate = fs::path(dir) / name;
    if (access(candidate.c_str(), X_OK) == 0)
      return candidate;
  }
  return std::nullopt;
}

// pio est souvent installé par l'extension VS Code dans ~/.platformio/penv/bin
static std::string pio_cmd() {
  if (find_in_path("pio"))
    return "pio";
  if (const char *home = std::getenv("HOME")) {
    fs::path p = fs::path(home) / ".platformio/penv/bin/pio";
    if (access(p.c_str(), X_OK) == 0)
      return p.string();
  }
  return "pio";
}

static unsigned jobs() {
  return std::max(1u, std::thread::hardware_concurrency());
}

static std::string fmt_secs(double s) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f s", s);
  return buf;
}

static std::string to_lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static bool contains(const std::string &s, const char *what) {
  return s.find(what) != std::string::npos;
}

// Supprime les séquences ANSI (couleurs, etc.) et les caractères de
// contr\u00f4le.
static std::string sanitize(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == 0x1b) {                               // ESC
      if (i + 1 < in.size() && in[i + 1] == '[') { // CSI ... lettre finale
        i += 2;
        while (i < in.size() && !(in[i] >= 0x40 && in[i] <= 0x7e))
          ++i;
      } else if (i + 1 < in.size() && in[i + 1] == ']') { // OSC ... BEL
        i += 2;
        while (i < in.size() && in[i] != '\a' && in[i] != 0x1b)
          ++i;
        if (i < in.size() && in[i] == 0x1b)
          ++i;
      } else {
        ++i;
      }
      continue;
    }
    if (c == '\t') {
      out += "    ";
      continue;
    }
    if (c < 0x20 || c == 0x7f)
      continue;
    out += static_cast<char>(c);
  }
  return out;
}

// Découpe "a 'b c' \"d\"" en arguments, comme un shell simplifié.
static std::vector<std::string> split_args(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  bool in_token = false;
  char quote = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == quote)
        quote = 0;
      else
        cur += c;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      in_token = true;
      continue;
    }
    if (c == '\\' && i + 1 < s.size()) {
      cur += s[++i];
      in_token = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (in_token) {
        out.push_back(cur);
        cur.clear();
        in_token = false;
      }
      continue;
    }
    cur += c;
    in_token = true;
  }
  if (in_token)
    out.push_back(cur);
  return out;
}

static std::vector<std::string> cat(std::vector<std::string> a,
                                    const std::vector<std::string> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// ============================================================================
// Ports série
// ============================================================================
struct SerialPort {
  std::string dev;   // /dev/ttyUSB0
  std::string label; // /dev/ttyUSB0 (usb-1a86_USB2.0-Serial...)
};

static std::vector<SerialPort> detect_ports() {
  std::map<std::string, std::string> by_id;
  std::error_code ec;
  for (const auto &e : fs::directory_iterator("/dev/serial/by-id", ec)) {
    std::error_code ec2;
    fs::path target = fs::canonical(e.path(), ec2);
    if (!ec2)
      by_id[target.string()] = e.path().filename().string();
  }

  std::vector<SerialPort> ports;
  std::error_code ec3;
  for (const auto &e : fs::directory_iterator("/dev", ec3)) {
    std::string name = e.path().filename().string();
    if (name.rfind("ttyUSB", 0) != 0 && name.rfind("ttyACM", 0) != 0)
      continue;
    SerialPort p{e.path().string(), e.path().string()};
    if (auto it = by_id.find(p.dev); it != by_id.end()) {
      std::string id = it->second;
      if (id.size() > 28)
        id = id.substr(0, 27) + "\u2026";
      p.label += "  (" + id + ")";
    }
    ports.push_back(p);
  }
  std::sort(
      ports.begin(), ports.end(),
      [](const SerialPort &a, const SerialPort &b) { return a.dev < b.dev; });
  return ports;
}

// ============================================================================
// \u00c9tapes / actions
// ============================================================================
enum class StepKind {
  Captured,    // sortie capturée et affichée dans le journal
  Interactive, // la TUI est suspendue, le programme a le terminal
  Internal     // code C++ exécuté par le launcher lui-m\u00eame
};

struct Step {
  StepKind kind = StepKind::Captured;
  std::string title;
  std::vector<std::string> argv;
  fs::path cwd;
  std::map<std::string, std::string> env;
  std::function<bool()> internal;
};

struct Action {
  std::string group;       // ESP / Studio / Global
  std::string label;       // texte du menu
  std::string cli;         // "esp build", "doctor", ...
  std::string description; // aide affichée sous le menu
  std::function<std::vector<Step>()> make_steps;
};

static std::string cmdline(const Step &s) {
  std::string out;
  for (const auto &[k, v] : s.env)
    out += k + "=" + v + " ";
  for (size_t i = 0; i < s.argv.size(); ++i) {
    if (i)
      out += ' ';
    const std::string &a = s.argv[i];
    if (a.empty() || a.find_first_of(" \t\"'") != std::string::npos)
      out += "'" + a + "'";
    else
      out += a;
  }
  return out;
}

// ============================================================================
// Journal (partagé entre le thread de travail et l'interface)
// ============================================================================
enum class LineKind { Normal, Command, Info, Success, Warning, Error };

struct LogLine {
  std::string text;
  LineKind kind;
};

static LineKind classify(const std::string &line) {
  std::string l = to_lower(line);
  if (contains(l, "[success]") || contains(l, "[passed]") ||
      contains(l, "100% tests passed"))
    return LineKind::Success;
  if (contains(l, "error") || contains(l, "erreur") ||
      contains(l, "[failed]") || contains(l, "fatal") ||
      contains(l, "undefined reference"))
    return LineKind::Error;
  if (contains(l, "warning") || contains(l, "avertissement"))
    return LineKind::Warning;
  return LineKind::Normal;
}

class Log {
public:
  std::function<void()> on_change; // TUI : demande un rafra\u00eechissement
  bool echo = false;               // CLI : écrit directement sur stdout

  void add(const std::string &raw, LineKind kind = LineKind::Normal) {
    std::string text = sanitize(raw);
    if (echo) {
      std::cout << text << std::endl;
      return;
    }
    {
      std::lock_guard<std::mutex> lk(m_);
      lines_.push_back({std::move(text), kind});
      while (lines_.size() > MAX_LOG_LINES)
        lines_.pop_front();
    }
    if (on_change)
      on_change();
  }

  void output(const std::string &raw) {
    std::string text = sanitize(raw);
    add(text, classify(text));
  }

  void clear() {
    {
      std::lock_guard<std::mutex> lk(m_);
      lines_.clear();
    }
    if (on_change)
      on_change();
  }

  std::mutex &mutex() { return m_; }
  const std::deque<LogLine> &lines() const { return lines_; } // sous mutex()

private:
  std::mutex m_;
  std::deque<LogLine> lines_;
};

// ============================================================================
// Exécution de processus (fork/exec)
// ============================================================================

// Tout ce dont l'enfant a besoin est préparé AVANT fork() :
// après fork() dans un programme multi-thread, on évite les allocations.
struct Prepared {
  std::vector<std::string> env_storage;
  std::vector<char *> argv;
  std::vector<char *> envp;
  std::string cwd;

  Prepared(const Step &s, const std::map<std::string, std::string> &extra_env) {
    std::map<std::string, std::string> overrides = extra_env;
    for (const auto &[k, v] : s.env)
      overrides[k] = v;

    for (char **e = environ; e && *e; ++e) {
      std::string kv(*e);
      std::string key = kv.substr(0, kv.find('='));
      if (!overrides.count(key))
        env_storage.push_back(kv);
    }
    for (const auto &[k, v] : overrides)
      env_storage.push_back(k + "=" + v);

    for (auto &kv : env_storage)
      envp.push_back(kv.data());
    envp.push_back(nullptr);
    for (const auto &a : s.argv)
      argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    cwd = s.cwd.string();
  }
  Prepared(const Prepared &) = delete;
  Prepared &operator=(const Prepared &) = delete;
};

static void raw_write(const char *s) {
  ssize_t r = write(STDERR_FILENO, s, std::strlen(s));
  (void)r;
}

[[noreturn]] static void exec_child(const Prepared &pr) {
  // Les signaux ignorés par le parent restent ignorés après exec : on les
  // remet.
  std::signal(SIGINT, SIG_DFL);
  std::signal(SIGQUIT, SIG_DFL);
  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGPIPE, SIG_DFL);
  std::signal(SIGTSTP, SIG_DFL);
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, nullptr);

  if (!pr.cwd.empty() && chdir(pr.cwd.c_str()) != 0) {
    raw_write("Impossible d'entrer dans le dossier : ");
    raw_write(pr.cwd.c_str());
    raw_write("\n");
    _exit(127);
  }
  execvpe(pr.argv[0], pr.argv.data(), pr.envp.data());
  raw_write("Impossible de lancer \" ");
  raw_write(pr.argv[0]);
  raw_write(" \" : ");
  raw_write(std::strerror(errno));
  raw_write("\n");
  _exit(127);
}

static int wait_child(pid_t pid) {
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      return -1;
  }
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return -1;
}

class Runner {
public:
  // Lance la commande, renvoie chaque ligne de sortie (stdout + stderr) à
  // on_line.
  int run_captured(const Step &s,
                   const std::function<void(const std::string &)> &on_line) {
    Prepared pr(s, {{"PYTHONUNBUFFERED", "1"},
                    {"PLATFORMIO_DISABLE_PROGRESSBAR", "true"}});

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
      on_line(std::string("pipe() a échoué : ") + std::strerror(errno));
      return -1;
    }
    int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
      on_line(std::string("fork() a échoué : ") + std::strerror(errno));
      close(fds[0]);
      close(fds[1]);
      if (devnull >= 0)
        close(devnull);
      return -1;
    }
    if (pid == 0) {
      setpgid(0, 0); // groupe à part : permet d'annuler toute l'arborescence
      if (devnull >= 0)
        dup2(devnull, STDIN_FILENO);
      dup2(fds[1], STDOUT_FILENO);
      dup2(fds[1], STDERR_FILENO);
      exec_child(pr);
    }

    setpgid(pid, pid);
    pgid_ = pid;
    close(fds[1]);
    if (devnull >= 0)
      close(devnull);

    // Lecture ligne par ligne. Un '\r' seul (barre de progression) écrase la
    // ligne.
    std::string cur;
    bool pending_cr = false;
    char buf[4096];
    while (true) {
      ssize_t n = read(fds[0], buf, sizeof buf);
      if (n < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      if (n == 0)
        break;
      for (ssize_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (pending_cr) {
          pending_cr = false;
          if (c != '\n')
            cur.clear();
        }
        if (c == '\r') {
          pending_cr = true;
          continue;
        }
        if (c == '\n') {
          on_line(cur);
          cur.clear();
          continue;
        }
        cur += c;
      }
    }
    if (!cur.empty())
      on_line(cur);
    close(fds[0]);

    int code = wait_child(pid);
    pgid_ = 0;
    return code;
  }

  // Lance la commande au premier plan : elle hérite du terminal (moniteur
  // série, studio...). Ctrl+C arr\u00eate l'enfant mais pas le launcher.
  int run_foreground(const Step &s) {
    Prepared pr(s, {});
    struct sigaction ign {
    }, old_int{}, old_quit{};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old_int);
    sigaction(SIGQUIT, &ign, &old_quit);

    pid_t pid = fork();
    if (pid == 0)
      exec_child(pr);
    int code = pid < 0 ? -1 : wait_child(pid);

    sigaction(SIGINT, &old_int, nullptr);
    sigaction(SIGQUIT, &old_quit, nullptr);
    return code;
  }

  void signal_job(int sig) {
    pid_t g = pgid_;
    if (g > 0)
      kill(-g, sig);
  }

private:
  std::atomic<pid_t> pgid_{0};
};

// ============================================================================
// Application
// ============================================================================
class App {
public:
  explicit App(Paths paths);
  int run_tui();
  int run_cli(int argc, char **argv);

private:
  using InteractiveFn = std::function<int(const Step &)>;
  enum class Result { None, Ok, Failed, Cancelled };

  // --- construction des étapes ---
  void add(std::string group, std::string label, std::string cli,
           std::string desc, std::function<std::vector<Step>()> make);
  Step pio_step(std::vector<std::string> args,
                StepKind kind = StepKind::Captured,
                std::string title = {}) const;
  Step studio_step(std::vector<std::string> argv, std::string title = {}) const;
  Step internal_step(std::string title, std::function<bool()> fn) const;
  std::vector<std::string> port_flag(const char *flag) const;
  std::string selected_port() const;
  std::string studio_port() const;
  std::vector<std::string> studio_args() const;

  std::vector<Step> esp_build() const;
  std::vector<Step> esp_upload() const;
  std::vector<Step> esp_monitor() const;
  std::vector<Step> esp_test() const;
  std::vector<Step> esp_clean() const;
  std::vector<Step> esp_compiledb() const;
  std::vector<Step> studio_configure() const;
  std::vector<Step> studio_build() const;
  std::vector<Step> studio_run();
  std::vector<Step> studio_test() const;
  std::vector<Step> studio_clean();
  std::vector<Step> studio_reset();

  bool doctor();

  // --- exécution ---
  bool execute(const std::vector<Step> &steps, const InteractiveFn &interactive,
               int &code);
  void start(size_t index);
  void cancel();
  void quit();
  int interactive_from_worker(const Step &s);
  int run_in_terminal(const Step &s);

  // --- interface ---
  bool refresh_ports(bool announce);
  void scroll_by(int delta);
  Element render(Component menu, Component run_btn, Component ports,
                 Component args);
  Element render_log();
  Element render_status();
  void print_help() const;

  Paths paths_;
  std::vector<Action> actions_;
  std::vector<std::string> menu_entries_;
  int menu_sel_ = 0;

  std::vector<SerialPort> ports_;
  std::vector<std::string> port_entries_;
  int port_sel_ = 0;
  std::string args_text_;

  std::optional<std::string> cli_port_;
  std::optional<std::vector<std::string>> cli_args_;

  Log log_;
  Runner runner_;
  ScreenInteractive screen_ = ScreenInteractive::Fullscreen();
  std::thread worker_, ticker_;
  std::atomic<bool> busy_{false}, cancel_{false}, quitting_{false};

  bool follow_ = true; // le journal suit la dernière ligne
  int scroll_ = 0;     // ligne affichée en bas quand follow_ == false

  std::mutex state_m_; // protège les champs ci-dessous
  std::string job_label_, last_label_;
  Result last_result_ = Result::None;
  int last_code_ = 0;
  double last_secs_ = 0.0;
  std::chrono::steady_clock::time_point job_start_;
};

// ----------------------------------------------------------------------------
// Définition des actions
// ----------------------------------------------------------------------------
App::App(Paths paths) : paths_(std::move(paths)) {
  add("ESP", "Compiler", "esp build", "pio run : compile le firmware ESP8266.",
      [this] { return esp_build(); });
  add("ESP", "Flasher", "esp upload",
      "Compile puis téléverse le firmware sur l'ESP8266 (port choisi "
      "ci-dessous).",
      [this] { return esp_upload(); });
  add("ESP", "Moniteur série", "esp monitor",
      "Ouvre pio device monitor dans le terminal. Ctrl+C pour revenir au "
      "launcher.",
      [this] { return esp_monitor(); });
  //   add("ESP", "Tests (sur cible)", "esp test",
  //       "pio test : compile, flashe et exécute les tests Unity sur l'ESP.",
  //       [this] { return esp_test(); });
  //   add("ESP", "Nettoyer", "esp clean",
  //       "pio run -t clean : supprime les fichiers compilés du firmware.",
  //       [this] { return esp_clean(); });
  //   add("ESP", "compile_commands.json", "esp compiledb",
  //       "Régénère compile_commands.json (autocomplétion clangd / VS Code).",
  //       [this] { return esp_compiledb(); });

  add("Studio", "Configurer (CMake)", "studio configure",
      "cmake -S . -B build : (re)génère le système de build du studio.",
      [this] { return studio_configure(); });
  add("Studio", "Compiler", "studio build",
      "Compile le studio (configure automatiquement si nécessaire).",
      [this] { return studio_build(); });
  add("Studio", "Lancer", "studio run",
      "Compile puis lance le studio dans le terminal, avec les arguments "
      "ci-dessous. "
      "Le port série est transmis via SONICWAVE_PORT.",
      [this] { return studio_run(); });
  //   add("Studio", "Tests (ctest)", "studio test",
  //       "Compile puis lance ctest --output-on-failure.",
  //       [this] { return studio_test(); });
  //   add("Studio", "Nettoyer", "studio clean",
  //       "cmake --build build --target clean.",
  //       [this] { return studio_clean(); });
  //   add("Studio", "Réinitialiser build/", "studio reset",
  //       "Supprime entièrement le dossier build/ du studio (utile si le cache
  //       CMake est cassé).", [this] { return studio_reset(); });

  add("Global", "Tout compiler", "all build",
      "Compile le firmware ESP puis le studio.", [this] {
        auto s = esp_build();
        for (auto &x : studio_build())
          s.push_back(std::move(x));
        return s;
      });
  //   add("Global", "Tout tester", "all test",
  //       "Tests sur l'ESP puis tests du studio.",
  //       [this] {
  //         auto s = esp_test();
  //         for (auto& x : studio_test()) s.push_back(std::move(x));
  //         return s;
  //       });
  add("Global", "Tout nettoyer", "all clean",
      "Nettoie le firmware et le studio.", [this] {
        auto s = esp_clean();
        for (auto &x : studio_clean())
          s.push_back(std::move(x));
        return s;
      });
  add("Global", "Dev : flasher + lancer", "dev",
      "Flashe l'ESP, compile le studio puis le lance : la boucle de dev "
      "complète.",
      [this] {
        auto s = esp_upload();
        for (auto &x : studio_run())
          s.push_back(std::move(x));
        return s;
      });
  add("Global", "Doctor", "doctor",
      "Vérifie les outils, le projet, les ports série et le groupe dialout.",
      [this] {
        return std::vector<Step>{
            internal_step("Diagnostic", [this] { return doctor(); })};
      });

  for (const auto &a : actions_) {
    std::string g = a.group;
    if (g.size() < 7)
      g += std::string(7 - g.size(), ' ');
    menu_entries_.push_back(g + "\u2502 " + a.label);
  }
}

void App::add(std::string group, std::string label, std::string cli,
              std::string desc, std::function<std::vector<Step>()> make) {
  actions_.push_back({std::move(group), std::move(label), std::move(cli),
                      std::move(desc), std::move(make)});
}

Step App::pio_step(std::vector<std::string> args, StepKind kind,
                   std::string title) const {
  Step s;
  s.kind = kind;
  s.title = std::move(title);
  s.cwd = paths_.esp;
  s.argv.push_back(pio_cmd());
  s.argv.insert(s.argv.end(), args.begin(), args.end());
  return s;
}

Step App::studio_step(std::vector<std::string> argv, std::string title) const {
  Step s;
  s.title = std::move(title);
  s.cwd = paths_.studio;
  s.argv = std::move(argv);
  return s;
}

Step App::internal_step(std::string title, std::function<bool()> fn) const {
  Step s;
  s.kind = StepKind::Internal;
  s.title = std::move(title);
  s.internal = std::move(fn);
  return s;
}

std::string App::selected_port() const {
  if (cli_port_)
    return *cli_port_;
  if (port_sel_ > 0 && port_sel_ <= static_cast<int>(ports_.size()))
    return ports_[static_cast<size_t>(port_sel_ - 1)].dev;
  return {}; // "auto" : PlatformIO choisit
}

std::string App::studio_port() const {
  std::string p = selected_port();
  if (p.empty() && !ports_.empty())
    p = ports_.front().dev;
  return p;
}

std::vector<std::string> App::studio_args() const {
  if (cli_args_)
    return *cli_args_;
  return split_args(args_text_);
}

std::vector<std::string> App::port_flag(const char *flag) const {
  std::string p = selected_port();
  if (p.empty())
    return {};
  return {flag, p};
}

std::vector<Step> App::esp_build() const {
  return {pio_step({"run", "-e", PIO_ENV})};
}

std::vector<Step> App::esp_upload() const {
  return {pio_step(
      cat({"run", "-e", PIO_ENV, "-t", "upload"}, port_flag("--upload-port")))};
}

std::vector<Step> App::esp_monitor() const {
  return {
      pio_step(cat({"device", "monitor", "-e", PIO_ENV}, port_flag("--port")),
               StepKind::Interactive, "Moniteur série")};
}

std::vector<Step> App::esp_test() const {
  return {pio_step(cat(cat({"test", "-e", PIO_ENV}, port_flag("--upload-port")),
                       port_flag("--test-port")))};
}

std::vector<Step> App::esp_clean() const {
  return {pio_step({"run", "-e", PIO_ENV, "-t", "clean"})};
}

std::vector<Step> App::esp_compiledb() const {
  return {pio_step({"run", "-e", PIO_ENV, "-t", "compiledb"})};
}

std::vector<Step> App::studio_configure() const {
  return {studio_step({"cmake", "-S", ".", "-B", "build",
                       std::string("-DCMAKE_BUILD_TYPE=") + BUILD_TYPE,
                       "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"})};
}

std::vector<Step> App::studio_build() const {
  std::vector<Step> steps;
  std::error_code ec;
  if (!fs::exists(paths_.studio_build / "CMakeCache.txt", ec))
    steps = studio_configure();
  steps.push_back(
      studio_step({"cmake", "--build", "build", "-j", std::to_string(jobs())}));
  return steps;
}

std::vector<Step> App::studio_run() {
  std::vector<Step> steps = studio_build();
  fs::path exe = paths_.studio_build / STUDIO_EXE;

  steps.push_back(internal_step("", [this, exe] {
    std::error_code ec;
    if (fs::exists(exe, ec))
      return true;
    log_.add("Exécutable introuvable : " + exe.string(), LineKind::Error);
    log_.add(
        "Ajuste STUDIO_EXE en haut de main.cpp (nom passé à add_executable).",
        LineKind::Warning);
    return false;
  }));

  Step run =
      studio_step(cat({exe.string()}, studio_args()), "Lancement du studio");
  run.kind = StepKind::Interactive;
  std::string port = studio_port();
  if (!port.empty())
    run.env["SONICWAVE_PORT"] = port;
  steps.push_back(run);
  return steps;
}

std::vector<Step> App::studio_test() const {
  std::vector<Step> steps = studio_build();
  steps.push_back(
      studio_step({"ctest", "--test-dir", "build", "--output-on-failure"}));
  return steps;
}

std::vector<Step> App::studio_clean() {
  std::error_code ec;
  if (!fs::exists(paths_.studio_build / "CMakeCache.txt", ec)) {
    return {internal_step("", [this] {
      log_.add("Studio : rien à nettoyer (pas encore configuré).",
               LineKind::Info);
      return true;
    })};
  }
  return {studio_step({"cmake", "--build", "build", "--target", "clean"})};
}

std::vector<Step> App::studio_reset() {
  fs::path dir = paths_.studio_build;
  return {internal_step("Suppression de " + dir.string(), [this, dir] {
    std::error_code ec;
    auto n = fs::remove_all(dir, ec);
    if (ec) {
      log_.add("\u00c9chec de la suppression : " + ec.message(),
               LineKind::Error);
      return false;
    }
    log_.add(std::to_string(n) + " élément(s) supprimé(s).", LineKind::Success);
    return true;
  })};
}

// ----------------------------------------------------------------------------
// Doctor
// ----------------------------------------------------------------------------
bool App::doctor() {
  bool all_ok = true;
  auto good = [&](const std::string &m) {
    log_.add("  \u2714 " + m, LineKind::Success);
  };
  auto warn = [&](const std::string &m) {
    log_.add("  \u26a0 " + m, LineKind::Warning);
  };
  auto bad = [&](const std::string &m) {
    log_.add("  \u2718 " + m, LineKind::Error);
    all_ok = false;
  };
  std::error_code ec;

  log_.add("Outils", LineKind::Info);
  if (auto p = find_in_path("pio")) {
    good("pio : " + p->string());
  } else if (std::string c = pio_cmd(); c != "pio") {
    warn("pio absent du PATH, utilisation de " + c);
  } else {
    bad("PlatformIO introuvable (pipx install platformio, ou extension VS "
        "Code)");
  }
  for (const char *tool : {"cmake", "ctest", "g++", "git"}) {
    if (auto p = find_in_path(tool))
      good(std::string(tool) + " : " + p->string());
    else
      bad(std::string(tool) +
          " introuvable (sudo apt install build-essential cmake git)");
  }
  if (!find_in_path("make") && !find_in_path("ninja"))
    bad("ni make ni ninja n'ont été trouvés");

  log_.add("Projet", LineKind::Info);
  good("racine : " + paths_.root.string());
  fs::path ini = paths_.esp / "platformio.ini";
  if (fs::exists(ini, ec)) {
    good("sonicwave-control/platformio.ini");
    std::ifstream f(ini);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    if (fs::is_directory(paths_.esp / "tests", ec) &&
        !contains(content, "test_dir"))
      warn("platformio.ini : ajoute \" test_dir = tests \" (PlatformIO cherche "
           "test/ par défaut)");
  } else {
    bad("platformio.ini introuvable dans sonicwave-control/");
  }
  if (fs::exists(paths_.studio / "CMakeLists.txt", ec))
    good("sonicwave-studio/CMakeLists.txt");
  else
    bad("CMakeLists.txt introuvable dans sonicwave-studio/");
  fs::path exe = paths_.studio_build / STUDIO_EXE;
  if (fs::exists(exe, ec))
    good("exécutable studio : " + exe.string());
  else
    warn("exécutable studio absent (" + exe.string() +
         ") : pas encore compilé, ou STUDIO_EXE à ajuster");

  log_.add("Ports série", LineKind::Info);
  auto ports = detect_ports();
  if (ports.empty()) {
    warn("aucun /dev/ttyUSB* ou /dev/ttyACM* (carte branchée ? câble de "
         "données ?)");
    if (fs::exists("/usr/bin/brltty", ec))
      warn("brltty est installé : sur Ubuntu il accapare souvent les puces "
           "CH340 "
           "\u2192 sudo apt remove brltty");
  }
  for (const auto &p : ports) {
    if (access(p.dev.c_str(), R_OK | W_OK) == 0)
      good(p.label + " accessible");
    else
      bad(p.label + " : pas de droits en lecture/écriture");
  }

  log_.add("Groupe dialout", LineKind::Info);
  if (struct group *g = getgrnam("dialout")) {
    gid_t gid = g->gr_gid;
    int n = getgroups(0, nullptr);
    std::vector<gid_t> groups(static_cast<size_t>(std::max(n, 0)));
    if (n > 0)
      getgroups(n, groups.data());
    bool active = getegid() == gid ||
                  std::find(groups.begin(), groups.end(), gid) != groups.end();

    bool listed = false;
    struct passwd *pw = getpwuid(getuid());
    if (pw && g->gr_mem)
      for (char **m = g->gr_mem; *m; ++m)
        if (std::strcmp(*m, pw->pw_name) == 0)
          listed = true;

    if (active)
      good("tu fais partie du groupe dialout");
    else if (listed)
      warn("ajouté à dialout mais pas encore actif : déconnecte-toi puis "
           "reconnecte-toi");
    else
      bad("pas dans dialout \u2192 sudo usermod -aG dialout $USER, puis "
          "reconnexion");
  } else {
    warn("groupe dialout inexistant sur ce système");
  }

  log_.add(all_ok ? "Environnement OK."
                  : "Des problèmes ont été détectés (voir ci-dessus).",
           all_ok ? LineKind::Success : LineKind::Error);
  return all_ok;
}

// ----------------------------------------------------------------------------
// Exécution des étapes (commune TUI / CLI)
// ----------------------------------------------------------------------------
bool App::execute(const std::vector<Step> &steps,
                  const InteractiveFn &interactive, int &code) {
  code = 0;
  for (const auto &s : steps) {
    if (cancel_)
      return false;
    if (!s.title.empty())
      log_.add("\u25b6 " + s.title, LineKind::Info);

    switch (s.kind) {
    case StepKind::Internal:
      if (!s.internal()) {
        code = 1;
        return false;
      }
      break;

    case StepKind::Captured:
      log_.add("$ " + cmdline(s), LineKind::Command);
      code = runner_.run_captured(
          s, [this](const std::string &l) { log_.output(l); });
      if (code != 0) {
        if (!cancel_)
          log_.add("La commande s'est terminée avec le code " +
                       std::to_string(code),
                   LineKind::Error);
        return false;
      }
      break;

    case StepKind::Interactive:
      log_.add("$ " + cmdline(s) + "   [terminal]", LineKind::Command);
      code = interactive(s);
      if (code == 127 || code < 0) {
        log_.add("Impossible de lancer la commande.", LineKind::Error);
        return false;
      }
      log_.add("Retour au launcher (code " + std::to_string(code) + ").",
               LineKind::Info);
      code = 0; // quitter un moniteur avec Ctrl+C n'est pas un échec
      break;
    }
  }
  return true;
}

void App::start(size_t index) {
  if (index >= actions_.size())
    return;
  if (busy_) {
    log_.add("Une tâche est déjà en cours (\u00c9chap pour l'annuler).",
             LineKind::Warning);
    return;
  }
  if (worker_.joinable())
    worker_.join();

  const Action &action = actions_[index];
  std::vector<Step> steps =
      action.make_steps(); // lu sur le thread UI (port, args)
  std::string label = action.group + " \u203a " + action.label;
  {
    std::lock_guard<std::mutex> lk(state_m_);
    job_label_ = label;
    job_start_ = std::chrono::steady_clock::now();
  }
  busy_ = true;
  cancel_ = false;
  follow_ = true;

  worker_ = std::thread([this, steps = std::move(steps), label] {
    log_.add("");
    log_.add("\u2501\u2501\u2501 " + label + " \u2501\u2501\u2501",
             LineKind::Info);
    auto t0 = std::chrono::steady_clock::now();

    int code = 0;
    bool ok = execute(
        steps, [this](const Step &s) { return interactive_from_worker(s); },
        code);
    double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();

    Result r = ok ? Result::Ok : (cancel_ ? Result::Cancelled : Result::Failed);
    if (r == Result::Ok)
      log_.add("\u2714 Terminé en " + fmt_secs(secs), LineKind::Success);
    else if (r == Result::Cancelled)
      log_.add("\u25a0 Annulé", LineKind::Warning);
    else
      log_.add("\u2718 \u00c9chec après " + fmt_secs(secs), LineKind::Error);

    {
      std::lock_guard<std::mutex> lk(state_m_);
      last_label_ = label;
      last_result_ = r;
      last_code_ = code;
      last_secs_ = secs;
    }
    busy_ = false;
    screen_.PostEvent(Event::Custom);
  });
}

// Depuis le thread de travail : demande au thread UI de suspendre la TUI,
// de lancer le programme dans le vrai terminal, puis attend le résultat.
int App::interactive_from_worker(const Step &s) {
  auto promise = std::make_shared<std::promise<int>>();
  std::future<int> result = promise->get_future();
  screen_.Post(screen_.WithRestoredIO(
      [this, s, promise] { promise->set_value(run_in_terminal(s)); }));
  while (result.wait_for(std::chrono::milliseconds(100)) !=
         std::future_status::ready) {
    if (quitting_)
      return -1;
  }
  return result.get();
}

int App::run_in_terminal(const Step &s) {
  std::cout << "\033[2J\033[H"
            << "\u2501\u2501\u2501 SonicWave Launcher \u2014 "
            << (s.title.empty() ? "commande" : s.title)
            << " \u2501\u2501\u2501\n"
            << "$ " << cmdline(s) << "\n"
            << "(Ctrl+C pour arr\u00eater et revenir au launcher)\n\n"
            << std::flush;
  int code = runner_.run_foreground(s);
  std::cout << "\n\u2501\u2501\u2501 Terminé (code " << code
            << "). Appuie sur Entrée pour revenir au launcher\u2026 "
            << std::flush;
  std::string line;
  if (!std::getline(std::cin, line))
    std::cin.clear();
  return code;
}

void App::cancel() {
  if (!busy_)
    return;
  cancel_ = true;
  runner_.signal_job(SIGTERM);
  log_.add("Annulation demandée\u2026", LineKind::Warning);
}

void App::quit() {
  if (busy_)
    cancel();
  screen_.Exit();
}

// ----------------------------------------------------------------------------
// Interface
// ----------------------------------------------------------------------------
bool App::refresh_ports(bool announce) {
  std::string previous =
      (port_sel_ > 0 && port_sel_ <= static_cast<int>(ports_.size()))
          ? ports_[static_cast<size_t>(port_sel_ - 1)].dev
          : std::string();
  std::vector<SerialPort> found = detect_ports();
  bool changed = found.size() != ports_.size() ||
                 !std::equal(found.begin(), found.end(), ports_.begin(),
                             [](const SerialPort &a, const SerialPort &b) {
                               return a.dev == b.dev;
                             });
  ports_ = std::move(found);

  port_entries_.assign(1, "auto (PlatformIO choisit)");
  for (const auto &p : ports_)
    port_entries_.push_back(p.label);
  port_sel_ = 0;
  for (size_t i = 0; i < ports_.size(); ++i)
    if (ports_[i].dev == previous)
      port_sel_ = static_cast<int>(i + 1);

  if (announce)
    log_.add(ports_.empty()
                 ? "Aucun port série détecté."
                 : std::to_string(ports_.size()) + " port(s) série détecté(s).",
             LineKind::Info);
  return changed;
}

void App::scroll_by(int delta) {
  int n;
  {
    std::lock_guard<std::mutex> lk(log_.mutex());
    n = static_cast<int>(log_.lines().size());
  }
  if (n == 0)
    return;
  int cur = follow_ ? n - 1 : scroll_;
  cur = std::clamp(cur + delta, 0, n - 1);
  scroll_ = cur;
  follow_ = cur >= n - 1;
}

Element App::render_log() {
  Elements els;
  {
    std::lock_guard<std::mutex> lk(log_.mutex());
    const auto &lines = log_.lines();
    int n = static_cast<int>(lines.size());
    if (n == 0) {
      els.push_back(
          text("Aucune sortie. Choisis une action puis appuie sur Entrée.") |
          dim);
    } else {
      // On affiche les lignes jusqu'à "target", qui est collée en bas de la
      // vue.
      int target = follow_ ? n - 1 : std::clamp(scroll_, 0, n - 1);
      int from = std::max(0, target - 400);
      for (int i = from; i <= target; ++i) {
        const LogLine &l = lines[static_cast<size_t>(i)];
        Element e = text(l.text);
        switch (l.kind) {
        case LineKind::Command:
          e = e | bold | color(Color::Cyan);
          break;
        case LineKind::Info:
          e = e | bold | color(Color::Blue);
          break;
        case LineKind::Success:
          e = e | color(Color::Green);
          break;
        case LineKind::Warning:
          e = e | color(Color::Yellow);
          break;
        case LineKind::Error:
          e = e | color(Color::Red);
          break;
        case LineKind::Normal:
          break;
        }
        if (i == target)
          e = e | focus;
        els.push_back(e);
      }
    }
  }
  std::string title =
      follow_ ? " Journal "
              : " Journal \u2014 défilement manuel (Fin : revenir en bas) ";
  return window(text(title) | bold,
                vbox(std::move(els)) | vscroll_indicator | frame | flex);
}

Element App::render_status() {
  std::lock_guard<std::mutex> lk(state_m_);
  if (busy_) {
    double secs = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - job_start_)
                      .count();
    return text(" \u23f3 " + job_label_ + " \u2014 " + fmt_secs(secs) +
                "  (\u00c9chap pour annuler)") |
           color(Color::Yellow);
  }
  switch (last_result_) {
  case Result::Ok:
    return text(" \u2714 " + last_label_ + " \u2014 réussi en " +
                fmt_secs(last_secs_)) |
           color(Color::Green);
  case Result::Failed:
    return text(" \u2718 " + last_label_ + " \u2014 échec (code " +
                std::to_string(last_code_) + ")") |
           color(Color::Red);
  case Result::Cancelled:
    return text(" \u25a0 " + last_label_ + " \u2014 annulé") |
           color(Color::Yellow);
  case Result::None:
    break;
  }
  return text(" Pr\u00eat.") | dim;
}

Element App::render(Component menu, Component run_btn, Component ports,
                    Component args) {
  const Action &cur = actions_[static_cast<size_t>(
      std::clamp(menu_sel_, 0, static_cast<int>(actions_.size()) - 1))];
  auto section = [](const std::string &t) {
    return text(t) | bold | color(Color::Cyan);
  };

  Element left = window(text(" Commandes ") | bold,
                        vbox({
                            menu->Render() | vscroll_indicator | frame | flex,
                            separator(),
                            paragraph(cur.description) | dim,
                            run_btn->Render(),
                            separator(),
                            section("Port série"),
                            ports->Render(),
                            separator(),
                            section("Arguments du studio"),
                            args->Render() | borderLight,
                        })) |
                 size(WIDTH, EQUAL, 42);

  Element header = hbox({
      text(" \u266a SonicWave Launcher ") | bold | color(Color::Magenta),
      text(paths_.root.string()) | dim,
      filler(),
      busy_ ? text("\u25cf occupé ") | color(Color::Yellow)
            : text("\u25cb pr\u00eat ") | color(Color::Green),
  });

  Element help =
      text(" Entrée lancer \u00b7 Tab zone suivante \u00b7 \u00c9chap annuler "
           "\u00b7 PgPréc/PgSuiv défiler \u00b7 "
           "Fin suivre \u00b7 r ports \u00b7 l effacer \u00b7 q quitter") |
      dim;

  return vbox({
      header,
      hbox({left, render_log() | flex}) | flex,
      render_status(),
      help,
  });
}

int App::run_tui() {
  log_.on_change = [this] { screen_.PostEvent(Event::Custom); };
  refresh_ports(false);
  log_.add("Racine du projet : " + paths_.root.string(), LineKind::Info);
  log_.add("Choisis une action à gauche puis Entrée. \" Doctor \" vérifie ton "
           "environnement.",
           LineKind::Info);

  auto menu_opt = MenuOption::Vertical();
  menu_opt.on_enter = [this] { start(static_cast<size_t>(menu_sel_)); };
  Component menu = Menu(&menu_entries_, &menu_sel_, menu_opt);

  Component run_btn = Button(
      "\u25b6 Lancer", [this] { start(static_cast<size_t>(menu_sel_)); },
      ButtonOption::Ascii());

  Component port_box = Radiobox(&port_entries_, &port_sel_);

  InputOption input_opt;
  input_opt.multiline = false;
  Component args_in = Input(&args_text_, "ex : --verbose", input_opt);

  Component controls = Container::Vertical({menu, run_btn, port_box, args_in});
  Component ui = Renderer(controls, [this, menu, run_btn, port_box, args_in] {
    return render(menu, run_btn, port_box, args_in);
  });

  ui = CatchEvent(ui, [this, args_in](Event e) {
    if (e == Event::Escape) {
      cancel();
      return true;
    }
    if (e == Event::PageUp) {
      scroll_by(-15);
      return true;
    }
    if (e == Event::PageDown) {
      scroll_by(15);
      return true;
    }
    if (e.is_mouse() && (e.mouse().button == Mouse::WheelUp ||
                         e.mouse().button == Mouse::WheelDown)) {
      scroll_by(e.mouse().button == Mouse::WheelUp ? -3 : 3);
      return true;
    }
    if (args_in->Focused())
      return false; // on laisse taper librement les arguments
    if (e == Event::End) {
      follow_ = true;
      return true;
    }
    if (e == Event::Character('q')) {
      quit();
      return true;
    }
    if (e == Event::Character('r')) {
      refresh_ports(true);
      return true;
    }
    if (e == Event::Character('l')) {
      log_.clear();
      follow_ = true;
      scroll_ = 0;
      return true;
    }
    return false;
  });

  // Rafra\u00eechit le chrono pendant une tâche et surveille le branchement de
  // l'ESP.
  ticker_ = std::thread([this] {
    int tick = 0;
    while (!quitting_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      ++tick;
      if (tick % 8 == 0) {
        screen_.Post(std::function<void()>([this] {
          if (refresh_ports(false))
            log_.add("Ports série mis à jour.", LineKind::Info);
        }));
      }
      if (busy_ && tick % 4 == 0)
        screen_.PostEvent(Event::Custom);
    }
  });

  screen_.Loop(ui);

  // Sortie propre : on tue la tâche en cours et on attend les threads.
  quitting_ = true;
  cancel_ = true;
  runner_.signal_job(SIGKILL);
  if (worker_.joinable())
    worker_.join();
  if (ticker_.joinable())
    ticker_.join();
  return 0;
}

// ----------------------------------------------------------------------------
// Mode ligne de commande (sans TUI)
// ----------------------------------------------------------------------------
void App::print_help() const {
  std::cout << "Usage : sonicwave-launcher [action] [--port /dev/ttyUSB0] [-- "
               "args du studio]\n"
               "Sans argument : interface TUI.\n\nActions :\n";
  for (const auto &a : actions_) {
    std::string cli = a.cli;
    if (cli.size() < 18)
      cli += std::string(18 - cli.size(), ' ');
    std::cout << "  " << cli << a.group << " \u2014 " << a.label << "\n";
  }
}

int App::run_cli(int argc, char **argv) {
  log_.echo = true;
  std::vector<std::string> args(argv + 1, argv + argc);
  std::vector<std::string> words;

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &a = args[i];
    if (a == "--") {
      cli_args_ = std::vector<std::string>(
          args.begin() + static_cast<long>(i) + 1, args.end());
      break;
    }
    if (a == "-h" || a == "--help") {
      print_help();
      return 0;
    }
    if (a == "--port" && i + 1 < args.size()) {
      cli_port_ = args[++i];
      continue;
    }
    if (a.rfind("--port=", 0) == 0) {
      cli_port_ = a.substr(7);
      continue;
    }
    words.push_back(a);
  }

  std::string command;
  for (const auto &w : words)
    command += (command.empty() ? "" : " ") + w;

  refresh_ports(false);
  for (const auto &action : actions_) {
    if (action.cli != command)
      continue;
    int code = 0;
    bool ok = execute(
        action.make_steps(),
        [this](const Step &s) { return runner_.run_foreground(s); }, code);
    return ok ? 0 : (code != 0 ? code : 1);
  }

  std::cerr << "Action inconnue : \" " << command << " \"\n\n";
  print_help();
  return 2;
}

// ============================================================================
int main(int argc, char **argv) {
  std::optional<fs::path> root = find_root();
  if (!root) {
    std::cerr << "Racine du projet introuvable (dossier contenant "
                 "sonicwave-control et "
                 "sonicwave-studio).\nLance le launcher depuis BE_POO/ ou "
                 "définis SONICWAVE_ROOT.\n";
    return 1;
  }
  Paths paths{*root, *root / "sonicwave-control", *root / "sonicwave-studio",
              *root / "sonicwave-studio" / "build"};

  App app(paths);
  if (argc > 1)
    return app.run_cli(argc, argv);
  return app.run_tui();
}