#pragma once
#include "settings.h"

enum SaveGroup : uint8_t { SAVE_TIME = 1, SAVE_DATE = 2, SAVE_FORMAT = 4 };
enum class SaveStatus : uint8_t { Unknown, Accepted, Busy, Success, Error, Review };
struct SaveRequest {
  uint32_t id = 0;
  uint8_t groups = 0;
  DateTime local;
  bool fmt24h = false;
  bool confirmed = false;
};
struct SaveResult {
  SaveStatus status = SaveStatus::Unknown;
  uint8_t committed = 0;
  DateTime proposed;
  const char *detail = "Unknown request";
};
struct SaveBackend {
  bool (*read)(DateTime &standard);
  bool (*write)(const DateTime &standard);
  bool (*format)(bool fmt24h);
};
// Cooperative owner: submit never writes. Each service call performs at most
// one group operation. IDs are monotonic within a boot; stale IDs never write.
class SaveOwner {
 public:
  SaveStatus submit(const SaveRequest &request);
  SaveResult query(uint32_t id) const;
  void service(const Settings &settings, const SaveBackend &backend);
 private:
  SaveRequest request;
  SaveResult result;
  uint32_t high_water = 0;
};
extern SaveOwner clock_save;
uint32_t save_new_id();
