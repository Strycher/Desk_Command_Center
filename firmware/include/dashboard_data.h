/**
 * Dashboard Data — C++ structs matching bridge JSON schema.
 * Parsed from /api/dashboard response via ArduinoJson.
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

static constexpr uint8_t MAX_EVENTS = 20;
static constexpr uint8_t MAX_TASKS  = 20;
static constexpr uint8_t MAX_REPOS  = 10;
static constexpr uint8_t MAX_HA_ENTITIES = 20;
static constexpr uint8_t MAX_HOURLY = 12;

enum class SourceStatus : uint8_t { MISSING, OK, ERROR, STALE };

struct CalendarEvent {
    char     title[128];
    char     location[64];
    char     start_time[25];
    char     end_time[25];
    uint32_t color;
    char     source[16];
    bool     all_day;
};

struct TaskItem {
    char    title[128];
    char    due_date[25];
    uint8_t priority;       // 0=none, 1=high, 2=med, 3=low
    char    source[16];
    bool    completed;
};

struct HourlyForecast {
    char  time[6];          // "14:00"
    float temp;
    char  icon[16];
    float precip_chance;
};

struct WeatherData {
    float   temp_current;
    float   temp_high;
    float   temp_low;
    uint8_t humidity;
    char    condition[32];
    char    icon[16];
    float   precip_chance;
    HourlyForecast hourly[MAX_HOURLY];
    uint8_t hourly_count;
};

struct RepoStatus {
    char    name[64];
    uint8_t open_prs;
    uint8_t open_issues;
    char    ci_status[16];  // "passing", "failing", "pending"
};

struct HAEntity {
    char entity_id[64];
    char friendly_name[64];
    char state[32];
    char domain[16];
};

struct GitHubData {
    RepoStatus repos[MAX_REPOS];
    uint8_t    repo_count;
};

struct HAData {
    HAEntity entities[MAX_HA_ENTITIES];
    uint8_t  entity_count;
};

struct BeadsData {
    uint8_t open_count;
    uint8_t in_progress_count;
    uint8_t blocked_count;
};

struct ClaudeData {
    char status[16];        // "active", "idle", "offline"
    char current_task[128];
};

template <typename T>
struct SourceBlock {
    SourceStatus status;
    char         last_updated[25];
    T            data;
};

struct DashboardData {
    uint32_t last_updated_ms;

    SourceBlock<CalendarEvent[MAX_EVENTS]> google_calendar;
    uint8_t google_calendar_count;

    SourceBlock<CalendarEvent[MAX_EVENTS]> microsoft_calendar;
    uint8_t microsoft_calendar_count;

    SourceBlock<WeatherData>  weather;

    SourceBlock<TaskItem[MAX_TASKS]> unfocused_tasks;
    uint8_t unfocused_tasks_count;

    SourceBlock<TaskItem[MAX_TASKS]> monday_tasks;
    uint8_t monday_tasks_count;

    SourceBlock<GitHubData>   github;
    SourceBlock<HAData>       home_assistant;
    SourceBlock<BeadsData>    beads;
    SourceBlock<ClaudeData>   claude;
};

namespace DashboardParser {
    void parse(const JsonDocument& doc, DashboardData& out);
}
