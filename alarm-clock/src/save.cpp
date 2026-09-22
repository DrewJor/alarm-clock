#include "config.h"
#include "save.h"
#include "scheduler.h"

SaveOwner clock_save;
uint32_t save_new_id() { static uint32_t id = 0; return ++id; }

SaveStatus SaveOwner::submit(const SaveRequest &r) {
  if (r.id && r.id == request.id) return result.status;
  if (!r.id || r.id <= high_water) return SaveStatus::Review;
  if (result.status == SaveStatus::Accepted) return SaveStatus::Busy;
  request = r; high_water = r.id;
  result = SaveResult{};
  result.status = SaveStatus::Accepted;
  result.detail = "Saving";
  return result.status;
}
SaveResult SaveOwner::query(uint32_t id) const {
  return id && id == request.id ? result : SaveResult{};
}
void SaveOwner::service(const Settings &s, const SaveBackend &b) {
  if (result.status != SaveStatus::Accepted) return;
  uint8_t clock_groups = request.groups & (SAVE_TIME | SAVE_DATE);
  if (clock_groups && !(result.committed & clock_groups)) {
    DateTime local = request.local;
    if (clock_groups != (SAVE_TIME | SAVE_DATE)) {
      DateTime current;
      if (!b.read(current)) { result.status = SaveStatus::Error; result.detail = "Clock unavailable"; return; }
      current = current + TimeSpan(clock_offset(current, s) * 3600);
      local = DateTime(clock_groups & SAVE_DATE ? local.year() : current.year(),
                       clock_groups & SAVE_DATE ? local.month() : current.month(),
                       clock_groups & SAVE_DATE ? local.day() : current.day(),
                       clock_groups & SAVE_TIME ? local.hour() : current.hour(),
                       clock_groups & SAVE_TIME ? local.minute() : current.minute(),
                       clock_groups & SAVE_TIME ? local.second() : current.second());
    }
    DateTime standard;
    bool unique = local_to_standard(local, s, standard);
    result.proposed = standard + TimeSpan(clock_offset(standard, s) * 3600);
    if (standard.year() < YEAR_MIN || standard.year() > YEAR_MAX || !local.isValid()) {
      result.status = SaveStatus::Error; result.detail = "Date out of range"; return;
    }
    if (!unique && !request.confirmed) {
      result.status = SaveStatus::Review; result.detail = "DST time conflict"; return;
    }
    if (!b.write(standard)) { result.status = SaveStatus::Error; result.detail = "Clock write failed"; return; }
    result.committed |= clock_groups;
    return; // format goes to storage on the next loop pass
  }
  if ((request.groups & SAVE_FORMAT) && !(result.committed & SAVE_FORMAT)) {
    if (!b.format(request.fmt24h)) {
      result.status = SaveStatus::Error; result.detail = "Format not saved"; return;
    }
    result.committed |= SAVE_FORMAT;
  }
  result.status = SaveStatus::Success;
  result.detail = "Saved";
}
