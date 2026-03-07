/**
 * Navigation Bar — Implementation
 * 800 x 50px bottom bar with 5 icons. "More" opens submenu.
 */

#include "ui/nav_bar.h"
#include <Arduino.h>
#include "logger.h"

static const lv_color_t BAR_BG      = lv_color_hex(0x1a1a2e);
static const lv_color_t ICON_NORMAL  = lv_color_hex(0x888899);
static const lv_color_t ICON_ACTIVE  = lv_color_hex(0x6C63FF);

static constexpr int16_t BAR_HEIGHT  = 50;
static constexpr int16_t BTN_WIDTH   = 160;
static constexpr uint8_t NUM_MAIN    = 5;

struct NavItem {
    const char* icon;
    ScreenId    id;
};

static const NavItem mainItems[] = {
    { LV_SYMBOL_HOME,  ScreenId::HOME     },
    { LV_SYMBOL_BELL,  ScreenId::CALENDAR },
    { LV_SYMBOL_LIST,  ScreenId::TASKS    },
    { LV_SYMBOL_IMAGE, ScreenId::WEATHER  },
    { LV_SYMBOL_BARS,  ScreenId::_COUNT   },  // "More" — hamburger menu
};

/* Submenu items for "More" */
struct SubItem {
    const char* label;
    ScreenId    id;
};

static const SubItem moreItems[] = {
    { "DevOps",      ScreenId::DEVOPS      },
    { "Claude",      ScreenId::CLAUDE      },
    { "Home Asst",   ScreenId::HA          },
    { "Settings",    ScreenId::SETTINGS    },
    { "Diagnostics", ScreenId::DIAGNOSTICS },
};
static constexpr uint8_t MORE_COUNT = sizeof(moreItems) / sizeof(moreItems[0]);

static lv_obj_t* bar = nullptr;
static lv_obj_t* btnIcons[NUM_MAIN] = {};
static lv_obj_t* moreMenu = nullptr;
static ScreenId  _activeId = ScreenId::HOME;

static void closeMoreMenu() {
    if (moreMenu) {
        /* Use async delete — closeMoreMenu() is called from inside
           onMoreItemClick(), which is a callback on a child of moreMenu.
           Synchronous lv_obj_del() here frees the button mid-callback,
           causing a use-after-free crash when LVGL processes the release. */
        lv_obj_del_async(moreMenu);
        moreMenu = nullptr;
    }
}

static void onMoreItemClick(lv_event_t* e) {
    auto* item = (const SubItem*)lv_event_get_user_data(e);
    LOG_INFO("NAV: more → %s (screen %d)", item->label, (int)item->id);
    closeMoreMenu();
    ScreenManager::show(item->id, LV_SCR_LOAD_ANIM_FADE_ON, 200, false);
    NavBar::setActive(item->id);
}

static void showMoreMenu() {
    if (moreMenu) {
        closeMoreMenu();
        return;
    }

    lv_obj_t* layer = lv_layer_top();
    moreMenu = lv_obj_create(layer);
    lv_obj_set_size(moreMenu, 160, MORE_COUNT * 40 + 10);
    lv_obj_align(moreMenu, LV_ALIGN_BOTTOM_RIGHT, 0, -(BAR_HEIGHT + 5));
    lv_obj_set_style_bg_color(moreMenu, lv_color_hex(0x252540), 0);
    lv_obj_set_style_bg_opa(moreMenu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(moreMenu, 8, 0);
    lv_obj_set_style_border_width(moreMenu, 0, 0);
    lv_obj_set_style_pad_all(moreMenu, 5, 0);
    lv_obj_set_flex_flow(moreMenu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(moreMenu, 2, 0);

    for (uint8_t i = 0; i < MORE_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(moreMenu);
        lv_obj_set_size(btn, 145, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x33335a), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x4444aa), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, moreItems[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xDDDDEE), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, onMoreItemClick, LV_EVENT_CLICKED,
                            (void*)&moreItems[i]);
    }
}

static void onNavClick(lv_event_t* e) {
    auto idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx == 4) {
        showMoreMenu();
        return;
    }
    closeMoreMenu();
    ScreenManager::show(mainItems[idx].id, LV_SCR_LOAD_ANIM_FADE_ON, 200, false);
    NavBar::setActive(mainItems[idx].id);
}

void NavBar::create() {
    lv_obj_t* layer = lv_layer_top();

    bar = lv_obj_create(layer);
    lv_obj_set_size(bar, 800, BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, BAR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (uint8_t i = 0; i < NUM_MAIN; i++) {
        lv_obj_t* btn = lv_btn_create(bar);
        lv_obj_set_size(btn, BTN_WIDTH - 10, BAR_HEIGHT - 6);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, ICON_ACTIVE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, mainItems[i].icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, ICON_NORMAL, 0);
        lv_obj_center(icon);

        btnIcons[i] = icon;
        lv_obj_add_event_cb(btn, onNavClick, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }

    setActive(ScreenId::HOME);
    LOG_INFO("NAV: bar created");
}

void NavBar::setActive(ScreenId id) {
    _activeId = id;
    for (uint8_t i = 0; i < NUM_MAIN - 1; i++) {
        lv_color_t color = (mainItems[i].id == id) ? ICON_ACTIVE : ICON_NORMAL;
        lv_obj_set_style_text_color(btnIcons[i], color, 0);
    }
    /* "More" icon highlights if active screen is in submenu */
    bool moreActive = (id == ScreenId::DEVOPS || id == ScreenId::CLAUDE ||
                       id == ScreenId::HA || id == ScreenId::SETTINGS ||
                       id == ScreenId::DIAGNOSTICS);
    lv_obj_set_style_text_color(btnIcons[4],
                                moreActive ? ICON_ACTIVE : ICON_NORMAL, 0);
}
