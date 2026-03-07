/**
 * Dashboard Data Parser — ArduinoJson to DashboardData structs.
 * Handles missing/null sources gracefully.
 */

#include "dashboard_data.h"
#include <ArduinoJson.h>
#include "logger.h"

static SourceStatus parseStatus(const char* s) {
    if (!s) return SourceStatus::MISSING;
    if (strcmp(s, "ok") == 0) return SourceStatus::OK;
    if (strcmp(s, "error") == 0) return SourceStatus::ERROR;
    if (strcmp(s, "stale") == 0) return SourceStatus::STALE;
    return SourceStatus::MISSING;
}

static void copyStr(char* dst, size_t dstLen, const char* src) {
    if (src) {
        strncpy(dst, src, dstLen - 1);
        dst[dstLen - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

static void parseCalendarEvents(JsonArrayConst arr, CalendarEvent* events,
                                 uint8_t& count, uint8_t maxCount) {
    count = 0;
    if (arr.isNull()) return;
    for (JsonObjectConst obj : arr) {
        if (count >= maxCount) break;
        CalendarEvent& e = events[count];
        copyStr(e.title, sizeof(e.title), obj["title"]);
        copyStr(e.location, sizeof(e.location), obj["location"]);
        copyStr(e.start_time, sizeof(e.start_time), obj["start_time"]);
        copyStr(e.end_time, sizeof(e.end_time), obj["end_time"]);
        e.color = obj["color"] | 0x4285F4;
        copyStr(e.source, sizeof(e.source), obj["source"]);
        e.all_day = obj["all_day"] | false;
        count++;
    }
}

static void parseTaskItems(JsonArrayConst arr, TaskItem* items,
                            uint8_t& count, uint8_t maxCount) {
    count = 0;
    if (arr.isNull()) return;
    for (JsonObjectConst obj : arr) {
        if (count >= maxCount) break;
        TaskItem& t = items[count];
        copyStr(t.title, sizeof(t.title), obj["title"]);
        copyStr(t.due_date, sizeof(t.due_date), obj["due_date"]);
        t.priority = obj["priority"] | 0;
        copyStr(t.source, sizeof(t.source), obj["source"]);
        t.completed = obj["completed"] | false;
        count++;
    }
}

template <typename T>
static void parseSourceMeta(JsonObjectConst src, SourceBlock<T>& block) {
    block.status = parseStatus(src["status"]);
    copyStr(block.last_updated, sizeof(block.last_updated), src["last_updated"]);
}

void DashboardParser::parse(const JsonDocument& doc, DashboardData& out) {
    /* No memset — preserve stale data for sources the bridge omits.
     * Each source section fully overwrites its block when present,
     * or sets status = MISSING when absent. */
    out.last_updated_ms = millis();

    JsonObjectConst sources = doc["sources"];
    if (sources.isNull()) {
        LOG_WARN("PARSE: no 'sources' key in response");
        return;
    }

    /* Google Calendar — bridge key: calendar_google
       data.calendars.<name>.events[] with {title, start, end, all_day, location, color_id} */
    JsonObjectConst gc = sources["calendar_google"];
    if (!gc.isNull()) {
        parseSourceMeta(gc, out.google_calendar);
        JsonObjectConst gcData = gc["data"];
        if (!gcData.isNull()) {
            JsonObjectConst cals = gcData["calendars"];
            out.google_calendar_count = 0;
            for (JsonPairConst cal : cals) {
                JsonArrayConst events = cal.value()["events"];
                for (JsonObjectConst ev : events) {
                    if (out.google_calendar_count >= MAX_EVENTS) break;
                    CalendarEvent& e = out.google_calendar.data[out.google_calendar_count];
                    copyStr(e.title, sizeof(e.title), ev["title"]);
                    copyStr(e.location, sizeof(e.location), ev["location"]);
                    copyStr(e.start_time, sizeof(e.start_time), ev["start"]);
                    copyStr(e.end_time, sizeof(e.end_time), ev["end"]);
                    e.color = ev["color_id"] | 0x4285F4;
                    copyStr(e.source, sizeof(e.source), "google");
                    e.all_day = ev["all_day"] | false;
                    out.google_calendar_count++;
                }
            }
        }
    } else {
        out.google_calendar.status = SourceStatus::MISSING;
    }

    /* Microsoft Calendar — bridge key: calendar_ms */
    JsonObjectConst mc = sources["calendar_ms"];
    if (!mc.isNull()) {
        parseSourceMeta(mc, out.microsoft_calendar);
        /* Same nested structure as Google if/when implemented */
    } else {
        out.microsoft_calendar.status = SourceStatus::MISSING;
    }

    /* Weather — bridge: data.current.{temp,humidity,condition,icon}, data.high, data.low */
    JsonObjectConst w = sources["weather"];
    if (!w.isNull()) {
        parseSourceMeta(w, out.weather);
        JsonObjectConst wd = w["data"];
        if (!wd.isNull()) {
            JsonObjectConst cur = wd["current"];
            out.weather.data.temp_current = cur["temp"] | 0.0f;
            out.weather.data.temp_high = wd["high"] | 0.0f;
            out.weather.data.temp_low = wd["low"] | 0.0f;
            out.weather.data.humidity = cur["humidity"] | 0;
            copyStr(out.weather.data.condition, sizeof(out.weather.data.condition),
                    cur["condition"]);
            copyStr(out.weather.data.icon, sizeof(out.weather.data.icon), cur["icon"]);
            out.weather.data.precip_chance = cur["precip_chance"] | 0.0f;

            JsonArrayConst hourly = wd["hourly"];
            out.weather.data.hourly_count = 0;
            if (!hourly.isNull()) {
                for (JsonObjectConst h : hourly) {
                    if (out.weather.data.hourly_count >= MAX_HOURLY) break;
                    HourlyForecast& hf = out.weather.data.hourly[out.weather.data.hourly_count];
                    copyStr(hf.time, sizeof(hf.time), h["time"]);
                    hf.temp = h["temp"] | 0.0f;
                    copyStr(hf.icon, sizeof(hf.icon), h["icon"]);
                    hf.precip_chance = h["precip_chance"] | 0.0f;
                    out.weather.data.hourly_count++;
                }
            }
        }
    } else {
        out.weather.status = SourceStatus::MISSING;
    }

    /* Unfocused Tasks */
    JsonObjectConst ut = sources["unfocused_tasks"];
    if (!ut.isNull()) {
        parseSourceMeta(ut, out.unfocused_tasks);
        parseTaskItems(ut["data"], out.unfocused_tasks.data,
                      out.unfocused_tasks_count, MAX_TASKS);
    } else {
        out.unfocused_tasks.status = SourceStatus::MISSING;
    }

    /* Monday Tasks */
    JsonObjectConst mt = sources["monday_tasks"];
    if (!mt.isNull()) {
        parseSourceMeta(mt, out.monday_tasks);
        parseTaskItems(mt["data"], out.monday_tasks.data,
                      out.monday_tasks_count, MAX_TASKS);
    } else {
        out.monday_tasks.status = SourceStatus::MISSING;
    }

    /* GitHub — bridge: data.repos is a dict keyed by "owner/name" */
    JsonObjectConst gh = sources["github"];
    if (!gh.isNull()) {
        parseSourceMeta(gh, out.github);
        JsonObjectConst ghd = gh["data"];
        if (!ghd.isNull()) {
            JsonObjectConst repos = ghd["repos"];
            out.github.data.repo_count = 0;
            for (JsonPairConst kv : repos) {
                if (out.github.data.repo_count >= MAX_REPOS) break;
                RepoStatus& rs = out.github.data.repos[out.github.data.repo_count];
                copyStr(rs.name, sizeof(rs.name), kv.key().c_str());
                JsonObjectConst r = kv.value();
                rs.open_prs = r["open_prs"] | 0;
                rs.open_issues = r["open_issues"] | 0;
                const char* ci = r["ci"];
                copyStr(rs.ci_status, sizeof(rs.ci_status), ci);
                out.github.data.repo_count++;
            }
        }
    } else {
        out.github.status = SourceStatus::MISSING;
    }

    /* Home Assistant — bridge: data.domains is dict of domain→[entity,...] */
    JsonObjectConst ha = sources["home_assistant"];
    if (!ha.isNull()) {
        parseSourceMeta(ha, out.home_assistant);
        JsonObjectConst haData = ha["data"];
        out.home_assistant.data.entity_count = 0;
        if (!haData.isNull()) {
            JsonObjectConst domains = haData["domains"];
            for (JsonPairConst domKv : domains) {
                const char* domainName = domKv.key().c_str();
                JsonArrayConst entities = domKv.value();
                for (JsonObjectConst e : entities) {
                    if (out.home_assistant.data.entity_count >= MAX_HA_ENTITIES) break;
                    HAEntity& ent = out.home_assistant.data.entities[out.home_assistant.data.entity_count];
                    copyStr(ent.entity_id, sizeof(ent.entity_id), e["entity_id"]);
                    copyStr(ent.friendly_name, sizeof(ent.friendly_name), e["friendly_name"]);
                    copyStr(ent.state, sizeof(ent.state), e["state"]);
                    copyStr(ent.domain, sizeof(ent.domain), domainName);
                    out.home_assistant.data.entity_count++;
                }
            }
        }
    } else {
        out.home_assistant.status = SourceStatus::MISSING;
    }

    /* Beads — bridge: data.{total_open, total_in_progress} */
    JsonObjectConst bd = sources["beads"];
    if (!bd.isNull()) {
        parseSourceMeta(bd, out.beads);
        JsonObjectConst bdd = bd["data"];
        if (!bdd.isNull()) {
            out.beads.data.open_count = bdd["total_open"] | 0;
            out.beads.data.in_progress_count = bdd["total_in_progress"] | 0;
            out.beads.data.blocked_count = bdd["blocked_count"] | 0;
        }
    } else {
        out.beads.status = SourceStatus::MISSING;
    }

    /* Claude */
    JsonObjectConst cl = sources["claude"];
    if (!cl.isNull()) {
        parseSourceMeta(cl, out.claude);
        JsonObjectConst cld = cl["data"];
        if (!cld.isNull()) {
            copyStr(out.claude.data.status, sizeof(out.claude.data.status), cld["status"]);
            copyStr(out.claude.data.current_task, sizeof(out.claude.data.current_task),
                    cld["current_task"]);
        }
    } else {
        out.claude.status = SourceStatus::MISSING;
    }

    LOG_INFO("PARSE: done — cal=%d+%d tasks=%d+%d repos=%d ha=%d",
                  out.google_calendar_count, out.microsoft_calendar_count,
                  out.unfocused_tasks_count, out.monday_tasks_count,
                  out.github.data.repo_count, out.home_assistant.data.entity_count);
}
