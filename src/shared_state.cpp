#include "shared_state.h"

void SharedState::begin() {
  mutex_ = xSemaphoreCreateMutex();
}

void SharedState::publishStatus(const String &title, const String &subtitle, bool loading) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  status_.title = title;
  status_.subtitle = subtitle;
  status_.loading = loading;
  statusDirty_ = true;
  xSemaphoreGive(mutex_);
}

bool SharedState::tryConsumeStatus(Status &out) {
  bool hadUpdate = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (statusDirty_) {
    out = status_;
    statusDirty_ = false;
    hadUpdate = true;
  }
  xSemaphoreGive(mutex_);
  return hadUpdate;
}

void SharedState::publishDashboard(const DashboardSnapshot &snapshot) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  dashboard_ = snapshot;
  dashboardDirty_ = true;
  xSemaphoreGive(mutex_);
}

bool SharedState::tryConsumeDashboard(DashboardSnapshot &out) {
  bool hadUpdate = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (dashboardDirty_) {
    out = dashboard_;
    dashboardDirty_ = false;
    hadUpdate = true;
  }
  xSemaphoreGive(mutex_);
  return hadUpdate;
}
