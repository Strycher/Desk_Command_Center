/**
 * Dashboard Data Parser — ArduinoJson to DashboardData structs.
 * Handles missing/null sources gracefully.
 */

#include "dashboard_data.h"
#include <ArduinoJson.h>

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
    memset(&out, 0, sizeof(out));
    out.last_updated_ms = millis();

    JsonObjectConst sources = doc["sources"];
    if (sources.isNull()) {
        Serial.println("PARSE: no 'sources' key in response");
        return;
    }

    /* Google Calendar */
    JsonObjectConst gc = sources["google_calendar"];
    if (!gc.isNull()) {
        parseSourceMeta(gc, out.google_calendar);
        parseCalendarEvents(gc["data"], out.google_calendar.data,
                           out.google_calendar_count, MAX_EVENTS);
    } else {
        out.google_calendar.status = SourceStatus::MISSING;
    }

    /* Microsoft Calendar */
    JsonObjectConst mc = sources["microsoft_calendar"];
    if (!mc.isNull()) {
        parseSourceMeta(mc, out.microsoft_calendar);
        parseCalendarEvents(mc["data"], out.microsoft_calendar.data,
                           out.microsoft_calendar_count, MAX_EVENTS);
    } else {
        out.microsoft_calendar.status = SourceStatus::MISSING;
    }

    /* Weather */
    JsonObjectConst w = sources["weather"];
    if (!w.isNull()) {
        parseSourceMeta(w, out.weather);
        JsonObjectConst wd = w["data"];
        if (!wd.isNull()) {
            out.weather.data.temp_current = wd["temp_current"] | 0.0f;
            out.weather.data.temp_high = wd["temp_high"] | 0.0f;
            out.weather.data.temp_low = wd["temp_low"] | 0.0f;
            out.weather.data.humidity = wd["humidity"] | 0;
            copyStr(out.weather.data.condition, sizeof(out.weather.data.condition),
                    wd["condition"]);
            copyStr(out.weather.data.icon, sizeof(out.weather.data.icon), wd["icon"]);
            out.weather.data.precip_chance = wd["precip_chance"] | 0.0f;

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

    /* GitHub */
    JsonObjectConst gh = sources["github"];
    if (!gh.isNull()) {
        parseSourceMeta(gh, out.github);
        JsonObjectConst ghd = gh["data"];
        if (!ghd.isNull()) {
            JsonArrayConst repos = ghd["repos"];
            out.github.data.repo_count = 0;
            if (!repos.isNull()) {
                for (JsonObjectConst r : repos) {
                    if (out.github.data.repo_count >= MAX_REPOS) break;
                    RepoStatus& rs = out.github.data.repos[out.github.data.repo_count];
                    copyStr(rs.name, sizeof(rs.name), r["name"]);
                    rs.open_prs = r["open_prs"] | 0;
                    rs.open_issues = r["open_issues"] | 0;
                    copyStr(rs.ci_status, sizeof(rs.ci_status), r["ci_status"]);
                    out.github.data.repo_count++;
                }
            }
        }
    } else {
        out.github.status = SourceStatus::MISSING;
    }

    /* Home Assistant */
    JsonObjectConst ha = sources["home_assistant"];
    if (!ha.isNull()) {
        parseSourceMeta(ha, out.home_assistant);
        JsonArrayConst entities = ha["data"];
        out.home_assistant.data.entity_count = 0;
        if (!entities.isNull()) {
            for (JsonObjectConst e : entities) {
                if (out.home_assistant.data.entity_count >= MAX_HA_ENTITIES) break;
                HAEntity& ent = out.home_assistant.data.entities[out.home_assistant.data.entity_count];
                copyStr(ent.entity_id, sizeof(ent.entity_id), e["entity_id"]);
                copyStr(ent.friendly_name, sizeof(ent.friendly_name), e["friendly_name"]);
                copyStr(ent.state, sizeof(ent.state), e["state"]);
                copyStr(ent.domain, sizeof(ent.domain), e["domain"]);
                out.home_assistant.data.entity_count++;
            }
        }
    } else {
        out.home_assistant.status = SourceStatus::MISSING;
    }

    /* Beads */
    JsonObjectConst bd = sources["beads"];
    if (!bd.isNull()) {
        parseSourceMeta(bd, out.beads);
        JsonObjectConst bdd = bd["data"];
        if (!bdd.isNull()) {
            out.beads.data.open_count = bdd["open_count"] | 0;
            out.beads.data.in_progress_count = bdd["in_progress_count"] | 0;
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

    Serial.printf("PARSE: done — cal=%d+%d tasks=%d+%d repos=%d ha=%d\n",
                  out.google_calendar_count, out.microsoft_calendar_count,
                  out.unfocused_tasks_count, out.monday_tasks_count,
                  out.github.data.repo_count, out.home_assistant.data.entity_count);
}
