#include "smart_factory/historian.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void usage() {
  std::cerr << "Usage:\n"
            << "  smart-factory-storage-maintenance backup <historian_dir> <backup_dir>\n"
            << "  smart-factory-storage-maintenance verify <database_file>\n"
            << "  smart-factory-storage-maintenance restore <backup_database_file> <historian_dir>\n"
            << "\nrestore is OFFLINE maintenance: stop the backend before invoking it.\n";
}
}

int main(int argc, char** argv) {
  if (argc < 3) { usage(); return 2; }
  try {
    const std::string command = argv[1];
    if (command == "verify") {
      if (argc != 3) { usage(); return 2; }
      std::string detail;
      const bool ok = smart_factory::HistorianStore::verifyDatabaseFile(argv[2], &detail);
      std::cout << (ok ? "PASS: " : "FAIL: ") << detail << '\n';
      return ok ? 0 : 3;
    }
    if (command == "backup") {
      if (argc != 4) { usage(); return 2; }
      smart_factory::HistorianOptions options;
      options.backupEnabled = true;
      options.backupDirectory = argv[3];
      smart_factory::HistorianStore store(argv[2], options);
      const auto path = store.backupNow();
      std::string detail;
      if (!smart_factory::HistorianStore::verifyDatabaseFile(path, &detail)) {
        throw std::runtime_error("created backup failed verification: " + detail);
      }
      std::cout << path << '\n';
      return 0;
    }
    if (command == "restore") {
      if (argc != 4) { usage(); return 2; }
      smart_factory::HistorianStore::restoreBackup(argv[2], argv[3]);
      const std::string restored = std::string(argv[3]) + "/historian.sqlite3";
      std::string detail;
      if (!smart_factory::HistorianStore::verifyDatabaseFile(restored, &detail)) {
        throw std::runtime_error("restored database failed verification: " + detail);
      }
      std::cout << restored << '\n';
      return 0;
    }
    usage();
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "storage maintenance failed: " << e.what() << '\n';
    return 1;
  }
}
