﻿/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

 This file is part of KeeperRL.

 KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
 GNU General Public License as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program.
 If not, see http://www.gnu.org/licenses/ . */

#include "stdafx.h"
#include "gui_builder.h"
#include "clock.h"
#include "renderer.h"
#include "view_id.h"
#include "player_message.h"
#include "view.h"
#include "options.h"
#include "map_gui.h"
#include "campaign.h"
#include "retired_games.h"

using SDL::SDL_Keysym;
using SDL::SDL_Keycode;

GuiBuilder::GuiBuilder(Renderer& r, GuiFactory& g, Clock* c, Options* o, Callbacks call)
    : renderer(r), gui(g), clock(c), options(o), callbacks(call), gameSpeed(GameSpeed::NORMAL) {
}

void GuiBuilder::reset() {
  gameSpeed = GameSpeed::NORMAL;
  numSeenVillains = -1;
}

void GuiBuilder::setMapGui(MapGui* g) {
  mapGui = g;
}

const int legendLineHeight = 30;
const int titleLineHeight = legendLineHeight + 8;

int GuiBuilder::getStandardLineHeight() const {
  return legendLineHeight;
}

PGuiElem GuiBuilder::getHintCallback(const vector<string>& s) {
  return gui.mouseOverAction([this, s]() { callbacks.hint({s}); });
}

function<void()> GuiBuilder::getButtonCallback(UserInput input) {
  return [this, input]() { callbacks.input(input); };
}

void GuiBuilder::setCollectiveTab(CollectiveTab t) {
  if (collectiveTab != t) {
    collectiveTab = t;
    clearActiveButton();
    closeOverlayWindows();
  }
}

CollectiveTab GuiBuilder::getCollectiveTab() const {
  return collectiveTab;
}

void GuiBuilder::closeOverlayWindows() {
  // send a random id which wont be found
  callbacks.input({UserInputId::CREATURE_BUTTON, UniqueEntity<Creature>::Id()});
  callbacks.input({UserInputId::WORKSHOP, -1});
}

optional<int> GuiBuilder::getActiveButton(CollectiveTab tab) const {
  if (activeButton && activeButton->tab == tab)
    return activeButton->num;
  else
    return none;
}

void GuiBuilder::setActiveGroup(const string& group) {
  clearActiveButton();
  activeGroup = group;
}

void GuiBuilder::setActiveButton(CollectiveTab tab, int num, ViewId viewId, optional<string> group) {
  activeButton = {tab, num};
  mapGui->setButtonViewId(viewId);
  activeGroup = group;
}

bool GuiBuilder::clearActiveButton() {
  if (activeButton || activeGroup) {
    activeButton = none;
    activeGroup = none;
    mapGui->clearButtonViewId();
    return true;
  }
  return false;
}

PGuiElem GuiBuilder::drawCost(pair<ViewId, int> cost, ColorId color) {
  string costText = toString(cost.second);
  return GuiFactory::ListBuilder(gui)
      .addElemAuto(gui.rightMargin(5, gui.label(costText, colors[color])))
      .addElem(gui.viewObject(cost.first), 25)
      .buildHorizontalList();
}

PGuiElem GuiBuilder::getButtonLine(CollectiveInfo::Button button, int num, CollectiveTab tab) {
  GuiFactory::ListBuilder line(gui);
  line.addElem(gui.viewObject(button.viewId), 35);
  if (button.state != CollectiveInfo::Button::ACTIVE)
    line.addElem(gui.label(button.name + " " + button.count, colors[ColorId::GRAY], button.hotkey), 100);
  else
    line.addElem(gui.stack(
          gui.uiHighlightConditional([=] () { return getActiveButton(tab) == num; }),
          gui.label(button.name + " " + button.count, colors[ColorId::WHITE], button.hotkey)
          ), 100);
  if (button.cost)
    line.addBackElemAuto(drawCost(*button.cost));
  function<void()> buttonFun;
  ViewId viewId = button.viewId;
  if (button.state != CollectiveInfo::Button::INACTIVE)
    buttonFun = [this, num, viewId, tab] {
      if (getActiveButton(tab) == num)
        clearActiveButton();
      else {
        setCollectiveTab(tab);
        setActiveButton(tab, num, viewId, none);
      }
    };
  else {
    buttonFun = [] {};
  }
  return gui.stack(
      getHintCallback({capitalFirst(button.help)}),
      gui.buttonChar(buttonFun, !button.hotkeyOpensGroup ? button.hotkey : 0, true, true),
      line.buildHorizontalList());
}

static optional<int> getFirstActive(const vector<CollectiveInfo::Button>& buttons, int begin) {
  for (int i = begin; i < buttons.size() && buttons[i].groupName == buttons[begin].groupName; ++i)
    if (buttons[i].state == CollectiveInfo::Button::ACTIVE)
      return i;
  return none;
}

static optional<int> getNextActive(const vector<CollectiveInfo::Button>& buttons, int begin,
    optional<int> current) {
  if (!current)
    return getFirstActive(buttons, begin);
  CHECK(current >= begin);
  int i = *current;
  do {
    ++i;
    if (buttons[i].groupName != buttons[begin].groupName)
      i = begin;
    if (current == i)
      return none;
  } while (buttons[i].state != CollectiveInfo::Button::ACTIVE);
  return i;
}

GuiFactory::ListBuilder GuiBuilder::drawButtons(vector<CollectiveInfo::Button> buttons, CollectiveTab tab) {
  auto elems = gui.getListBuilder(legendLineHeight);
  string lastGroup;
  for (int i : All(buttons)) {
    if (!buttons[i].groupName.empty() && buttons[i].groupName != lastGroup) {
      lastGroup = buttons[i].groupName;
      function<void()> buttonFunHotkey = [=] {
        if (activeGroup != lastGroup) {
          setCollectiveTab(tab);
          if (auto firstBut = getNextActive(buttons, i, none))
            setActiveButton(tab, *firstBut, buttons[*firstBut].viewId, lastGroup);
          else
            setActiveGroup(lastGroup);
        } else
        if (auto newBut = getNextActive(buttons, i, getActiveButton(tab)))
          setActiveButton(tab, *newBut, buttons[*newBut].viewId, lastGroup);
        else
          clearActiveButton();
      };
      function<void()> labelFun = [=] {
        if (activeGroup != lastGroup) {
          clearActiveButton();
          setCollectiveTab(tab);
          activeGroup = lastGroup;
        } else {
          clearActiveButton();
        }
      };
      auto line = gui.getListBuilder();
      line.addElem(gui.viewObject(buttons[i].viewId), 35);
      char hotkey = buttons[i].hotkeyOpensGroup ? buttons[i].hotkey : 0;
      line.addElemAuto(gui.stack(
          gui.uiHighlightConditional([=] { return activeGroup == lastGroup;}),
          gui.label(lastGroup, colors[ColorId::WHITE], hotkey)));
      elems.addElem(gui.stack(
          gui.keyHandlerChar(buttonFunHotkey, hotkey, true, true),
          gui.button(labelFun),
          line.buildHorizontalList()));
    }
    if (buttons[i].groupName.empty())
      elems.addElem(getButtonLine(buttons[i], i, tab));
  }
  return elems;
}

PGuiElem GuiBuilder::drawBuildings(CollectiveInfo& info) {
  int newHash = combineHash(info.buildings);
  if (newHash != buildingsHash) {
    buildingsCache =  gui.scrollable(drawButtons(info.buildings, CollectiveTab::BUILDINGS).buildVerticalList(),
        &buildingsScroll, &scrollbarsHeld);
    buildingsHash = newHash;
  }
  return gui.external(buildingsCache.get());
}

PGuiElem GuiBuilder::drawTechnology(CollectiveInfo& info) {
  int hash = combineHash(info.techButtons, info.workshopButtons, info.libraryButtons);
  if (hash != technologyHash) {
    technologyHash = hash;
    auto lines = drawButtons(info.libraryButtons, CollectiveTab::TECHNOLOGY);
    lines.addSpace(legendLineHeight / 2);
    for (int i : All(info.techButtons)) {
      auto line = gui.getListBuilder();
      line.addElem(gui.viewObject(info.techButtons[i].viewId), 35);
      line.addElemAuto(gui.label(info.techButtons[i].name, colors[ColorId::WHITE], info.techButtons[i].hotkey));
      lines.addElem(gui.stack(
            gui.buttonChar([this, i] {
              closeOverlayWindows();
              getButtonCallback(UserInput(UserInputId::TECHNOLOGY, i))();
            }, info.techButtons[i].hotkey, true, true),
            line.buildHorizontalList()));
    }
    lines.addSpace(legendLineHeight / 2);
    for (int i : All(info.workshopButtons)) {
      auto& button = info.workshopButtons[i];
      auto line = gui.getListBuilder();
      line.addElem(gui.viewObject(button.viewId), 35);
      line.addElemAuto(gui.label(button.name, colors[button.unavailable ? ColorId::GRAY : ColorId::WHITE]));
      PGuiElem elem = line.buildHorizontalList();
      if (button.active)
        elem = gui.stack(
            gui.uiHighlight(colors[ColorId::GREEN]),
            std::move(elem));
      if (!button.unavailable)
        elem = gui.stack(
          gui.button([this, i] {
            workshopsScroll2 = workshopsScroll = 0;
            getButtonCallback(UserInput(UserInputId::WORKSHOP, i))(); }),
          std::move(elem));
      lines.addElem(std::move(elem));
    }
    technologyCache = lines.buildVerticalList();
  }
  return gui.external(technologyCache.get());
}

PGuiElem GuiBuilder::drawKeeperHelp() {
  if (!keeperHelp) {
    vector<string> helpText {
      "left mouse button: dig,build",
		"right mouse button: select minion",
		"or click to drag map",
		"",
        "mouse + shift: area select",
        "mouse + control: area deselect",
        "arrow keys: map scroll",
        "shift + arrows faster map scroll",
        "",
        "to control or show information",
        "Keys 1-4: change game speed"",
        "Space: pause",
        "A & Z or mouse wheel: Zoom",
        "press mouse wheel: level map",
        "",
        "follow the orange hints :-)"};
    auto lines = gui.getListBuilder(legendLineHeight);
    for (string line : helpText)
      lines.addElem(gui.label(line, colors[ColorId::LIGHT_BLUE]));
    keeperHelp = lines.buildVerticalList();
  }
  return gui.external(keeperHelp.get());
}

int GuiBuilder::FpsCounter::getSec() {
  return clock.getMillis().count() / 1000;
}

void GuiBuilder::FpsCounter::addTick() {
  if (getSec() == curSec)
    ++curFps;
  else {
    lastFps = curFps;
    curSec = getSec();
    curFps = 0;
  }
}

int GuiBuilder::FpsCounter::getFps() {
  return lastFps;
}

void GuiBuilder::addFpsCounterTick() {
  fpsCounter.addTick();
}

void GuiBuilder::addUpsCounterTick() {
  upsCounter.addTick();
}

const int resourceSpace = 110;

PGuiElem GuiBuilder::drawBottomBandInfo(GameInfo& gameInfo) {
  CollectiveInfo& info = gameInfo.collectiveInfo;
  GameSunlightInfo& sunlightInfo = gameInfo.sunlightInfo;
  if (!bottomBandCache) {
    auto topLine = gui.getListBuilder(resourceSpace);
    for (int i : All(info.numResource)) {
      auto res = gui.getListBuilder();
      res.addElem(gui.viewObject(info.numResource[i].viewId), 30);
      res.addElemAuto(gui.labelFun([&info, i] { return toString<int>(info.numResource[i].count); },
            [&info, i] { return info.numResource[i].count >= 0 ? colors[ColorId::WHITE] : colors[ColorId::RED]; }));
      topLine.addElem(gui.stack(getHintCallback({info.numResource[i].name}), res.buildHorizontalList()));
    }
    auto bottomLine = gui.getListBuilder(140);
    bottomLine.addElem(getTurnInfoGui(gameInfo.time));
    bottomLine.addElem(getSunlightInfoGui(sunlightInfo));
    bottomLine.addElem(gui.labelFun([&gameInfo] {
          return "population: " + toString(gameInfo.collectiveInfo.minionCount) + " / " +
          toString(gameInfo.collectiveInfo.minionLimit); }));
    bottomBandCache = gui.getListBuilder(28)
          .addElem(gui.centerHoriz(topLine.buildHorizontalList()))
          .addElem(gui.centerHoriz(bottomLine.buildHorizontalList()))
          .buildVerticalList();
  }
  return gui.external(bottomBandCache.get());
}

const char* GuiBuilder::getGameSpeedName(GuiBuilder::GameSpeed gameSpeed) const {
  switch (gameSpeed) {
    case GameSpeed::SLOW: return "slow";
    case GameSpeed::NORMAL: return "normal";
    case GameSpeed::FAST: return "fast";
    case GameSpeed::VERY_FAST: return "very fast";
  }
}

const char* GuiBuilder::getCurrentGameSpeedName() const {
  if (clock->isPaused())
    return "paused";
  else
    return getGameSpeedName(gameSpeed);
}

PGuiElem GuiBuilder::drawRightBandInfo(GameInfo& info) {
  int hash = combineHash(info.collectiveInfo, info.villageInfo, info.modifiedSquares, info.totalSquares);
  if (hash != rightBandInfoHash) {
    rightBandInfoHash = hash;
    CollectiveInfo& collectiveInfo = info.collectiveInfo;
    VillageInfo& villageInfo = info.villageInfo;
    vector<PGuiElem> buttons = makeVec<PGuiElem>(
        gui.icon(gui.BUILDING),
        gui.icon(gui.MINION),
        gui.icon(gui.LIBRARY),
        gui.stack(gui.conditional(gui.icon(gui.HIGHLIGHT, GuiFactory::Alignment::CENTER, colors[ColorId::YELLOW]),
                      [=] { return numSeenVillains < villageInfo.villages.size();}),
                  gui.icon(gui.DIPLOMACY)),
        gui.icon(gui.HELP)
    );
    for (int i : All(buttons)) {
      buttons[i] = gui.stack(
          gui.conditional(gui.icon(gui.HIGHLIGHT, GuiFactory::Alignment::CENTER, colors[ColorId::GREEN]),
            [this, i] { return int(collectiveTab) == i;}),
          std::move(buttons[i]),
          gui.button([this, i]() { setCollectiveTab(CollectiveTab(i)); }));
    }
    if (!info.singleModel)
      buttons.push_back(gui.stack(
            gui.conditional(gui.icon(gui.HIGHLIGHT, GuiFactory::Alignment::CENTER, colors[ColorId::GREEN]),
              [this] { return false;}),
            gui.icon(gui.WORLD_MAP),
            gui.button(getButtonCallback(UserInputId::DRAW_WORLD_MAP))));
    vector<pair<CollectiveTab, PGuiElem>> elems = makeVec<pair<CollectiveTab, PGuiElem>>(
        make_pair(CollectiveTab::MINIONS, drawMinions(collectiveInfo)),
        make_pair(CollectiveTab::BUILDINGS, drawBuildings(collectiveInfo)),
        make_pair(CollectiveTab::KEY_MAPPING, drawKeeperHelp()),
        make_pair(CollectiveTab::TECHNOLOGY, drawTechnology(collectiveInfo)),
        make_pair(CollectiveTab::VILLAGES, drawVillages(villageInfo)));
    vector<PGuiElem> tabs;
    for (auto& elem : elems) {
      auto tab = elem.first;
      tabs.push_back(gui.conditional(std::move(elem.second), [tab, this] { return tab == collectiveTab;}));
    }
    PGuiElem main = gui.stack(std::move(tabs));
    main = gui.margins(std::move(main), 15, 15, 15, 5);
    int numButtons = buttons.size();
    auto buttonList = gui.getListBuilder(50);
    for (auto& elem : buttons)
      buttonList.addElem(std::move(elem));
    PGuiElem butGui = gui.margins(
        gui.centerHoriz(buttonList.buildHorizontalList()), 0, 5, 9, 5);
    auto bottomLine = gui.getListBuilder();
    bottomLine.addElem(gui.stack(
        gui.getListBuilder()
            .addElem(gui.label("speed:"), 60)
            .addElemAuto(gui.labelFun([this] { return getCurrentGameSpeedName();},
              [this] { return colors[clock->isPaused() ? ColorId::RED : ColorId::WHITE]; })).buildHorizontalList(),
        gui.button([&] { gameSpeedDialogOpen = !gameSpeedDialogOpen; })), 160);
    int modifiedSquares = info.modifiedSquares;
    int totalSquares = info.totalSquares;
    bottomLine.addElemAuto(
        gui.labelFun([=]()->string {
            return "FPS " + toString(fpsCounter.getFps()) + " / " + toString(upsCounter.getFps());
                //+ " SMOD " + toString(modifiedSquares) + "/" + toString(totalSquares);
        },
        colors[ColorId::WHITE]));
    main = gui.margin(gui.leftMargin(25, bottomLine.buildHorizontalList()),
        std::move(main), 18, gui.BOTTOM);
    rightBandInfoCache = gui.margin(std::move(butGui), std::move(main), 55, gui.TOP);
  }
  return gui.external(rightBandInfoCache.get());
}

GuiBuilder::GameSpeed GuiBuilder::getGameSpeed() const {
  return gameSpeed;
}

void GuiBuilder::setGameSpeed(GameSpeed speed) {
  gameSpeed = speed;
}

static char getHotkeyChar(GuiBuilder::GameSpeed speed) {
  return '1' + int(speed);
}

static SDL_Keycode getHotkey(GuiBuilder::GameSpeed speed) {
  switch (speed) {
    case GuiBuilder::GameSpeed::SLOW: return SDL::SDLK_1;
    case GuiBuilder::GameSpeed::NORMAL: return SDL::SDLK_2;
    case GuiBuilder::GameSpeed::FAST: return SDL::SDLK_3;
    case GuiBuilder::GameSpeed::VERY_FAST: return SDL::SDLK_4;
  }
}

void GuiBuilder::drawGameSpeedDialog(vector<OverlayInfo>& overlays) {
  if (!speedDialog) {
    vector<PGuiElem> lines;
    int keyMargin = 95;
    lines.push_back(gui.stack(
          gui.getListBuilder(keyMargin)
              .addElem(gui.label("pause"))
              .addElem(gui.label("[space]")).buildHorizontalList(),
          gui.button([=] {
            if (clock->isPaused())
            clock->cont();
            else
            clock->pause();
            gameSpeedDialogOpen = false;
            }, gui.getKey(SDL::SDLK_SPACE))));
    for (GameSpeed speed : ENUM_ALL(GameSpeed)) {
      Color color = colors[speed == gameSpeed ? ColorId::GREEN : ColorId::WHITE];
      lines.push_back(gui.stack(gui.getListBuilder(keyMargin)
                .addElem(gui.label(getGameSpeedName(speed), color))
                .addElem(gui.label("'" + string(1, getHotkeyChar(speed)) + "' ", color)).buildHorizontalList(),
            gui.button([=] { gameSpeed = speed; gameSpeedDialogOpen = false; clock->cont();},
              gui.getKey(getHotkey(speed)))));
    }
    reverse(lines.begin(), lines.end());
    int margin = 20;
    Vec2 size(150 + 2 * margin, legendLineHeight * lines.size() - 10 + 2 * margin);
    PGuiElem dialog = gui.miniWindow(
        gui.margins(gui.verticalList(std::move(lines), legendLineHeight), margin));
    speedDialog = {gui.conditional(std::move(dialog), [this] { return gameSpeedDialogOpen; }), size,
      OverlayInfo::GAME_SPEED};
  }
  overlays.push_back({gui.external(speedDialog->elem.get()), speedDialog->size, speedDialog->alignment});
}

PGuiElem GuiBuilder::getSunlightInfoGui(GameSunlightInfo& sunlightInfo) {
  return gui.stack(
      gui.conditional(
          getHintCallback({"Time remaining till nightfall."}),
          getHintCallback({"Time remaining till day."}),
          [&] { return sunlightInfo.description == "day";}),
      gui.labelFun([&] { return sunlightInfo.description + " [" + toString(sunlightInfo.timeRemaining) + "]"; },
          [&] { return sunlightInfo.description == "day" ? colors[ColorId::WHITE] : colors[ColorId::LIGHT_BLUE];}));
}

PGuiElem GuiBuilder::getTurnInfoGui(int& turn) {
  return gui.stack(getHintCallback({"Current turn."}),
      gui.labelFun([&turn] { return "T: " + toString(turn); }, colors[ColorId::WHITE]));
}

static Color getBonusColor(int bonus) {
  if (bonus < 0)
    return colors[ColorId::RED];
  if (bonus > 0)
    return colors[ColorId::GREEN];
  return colors[ColorId::WHITE];
}

typedef PlayerInfo::AttributeInfo::Id AttrId;

static GuiFactory::IconId getAttrIcon(AttrId id) {
  switch (id) {
    case AttrId::ATT: return GuiFactory::STAT_ATT;
    case AttrId::DEF: return GuiFactory::STAT_DEF;
    case AttrId::STR: return GuiFactory::STAT_STR;
    case AttrId::DEX: return GuiFactory::STAT_DEX;
    case AttrId::ACC: return GuiFactory::STAT_ACC;
    case AttrId::SPD: return GuiFactory::STAT_SPD;
  }
}

vector<PGuiElem> GuiBuilder::drawPlayerAttributes(const vector<PlayerInfo::AttributeInfo>& attr) {
  vector<PGuiElem> ret;
  for (auto& elem : attr)
    ret.push_back(gui.stack(getTooltip({elem.name, elem.help}),
        gui.horizontalList(makeVec<PGuiElem>(
          gui.icon(getAttrIcon(elem.id)),
          gui.margins(gui.label(toString(elem.value), getBonusColor(elem.bonus)), 0, 2, 0, 0)), 30)));
  return ret;
}

PGuiElem GuiBuilder::drawBottomPlayerInfo(GameInfo& gameInfo) {
  return gui.getListBuilder(28)
      .addElem(gui.centerHoriz(gui.horizontalList(
              drawPlayerAttributes(gameInfo.playerInfo.attributes), resourceSpace)))
      .addElem(gui.centerHoriz(gui.getListBuilder(140)
            .addElem(getTurnInfoGui(gameInfo.time))
            .addElem(getSunlightInfoGui(gameInfo.sunlightInfo))
            .buildHorizontalList()))
      .buildVerticalList();
}

static int viewObjectWidth = 27;
vector<string> GuiBuilder::getItemHint(const ItemInfo& item) {
  vector<string> out { capitalFirst(item.fullName)};
  if (!item.description.empty())
    out.push_back(item.description);
  if (item.equiped)
    out.push_back("Equipped.");
  if (item.pending)
    out.push_back("Not equipped yet.");
  if (item.locked)
    out.push_back("Locked: minion won't change to another item.");
  if (!item.unavailableReason.empty())
    out.push_back(item.unavailableReason);
  return out;
}

PGuiElem GuiBuilder::getItemLine(const ItemInfo& item, function<void(Rectangle)> onClick,
    function<void()> onMultiClick) {
  GuiFactory::ListBuilder line(gui);
  int leftMargin = -4;
  if (item.locked) {
    line.addElem(gui.viewObject(ViewId::KEY), viewObjectWidth);
    leftMargin -= viewObjectWidth - 3;
  }
  line.addElem(gui.viewObject(item.viewId), viewObjectWidth);
  Color color = colors[item.equiped ? ColorId::GREEN : (item.pending || item.unavailable) ?
      ColorId::GRAY : ColorId::WHITE];
  if (item.number > 1)
    line.addElemAuto(gui.rightMargin(8, gui.label(toString(item.number), color)));
  if (!item.name.empty())
    line.addElemAuto(gui.label(item.name, color));
  else
    line.addSpace(130);
  for (auto& elem : line.getAllElems())
    elem = gui.stack(gui.buttonRect(onClick), std::move(elem), getTooltip(getItemHint(item)));
  if (item.owner) {
    line.addBackElem(gui.viewObject(item.owner->viewId), viewObjectWidth);
    line.addBackElem(gui.label("L:" + toString(item.owner->expLevel)), 60);
  }
  if (item.price)
    line.addBackElemAuto(drawCost(*item.price));
  if (onMultiClick && item.number > 1) {
    line.addBackElem(gui.stack(
        gui.label("[#]"),
        gui.button(onMultiClick),
        getTooltip({"Click to choose how many to pick up."})), 25);
  }
  return gui.margins(line.buildHorizontalList(), leftMargin, 0, 0, 0);
}

PGuiElem GuiBuilder::getTooltip(const vector<string>& text) {
  return gui.conditional(gui.tooltip(text), [this] { return !disableTooltip;});
}

const int listLineHeight = 30;
const int listBrokenLineHeight = 24;

int GuiBuilder::getScrollPos(int index, int count) {
  return max(0, min(count - 1, index - 3));
}

vector<SDL_Keysym> GuiBuilder::getConfirmationKeys() {
  return {gui.getKey(SDL::SDLK_RETURN), gui.getKey(SDL::SDLK_KP_ENTER), gui.getKey(SDL::SDLK_KP_5)};
}

void GuiBuilder::drawPlayerOverlay(vector<OverlayInfo>& ret, PlayerInfo& info) {
  if (info.lyingItems.empty()) {
    playerOverlayFocused = false;
    itemIndex = -1;
    return;
  }
  if (lastPlayerPositionHash && lastPlayerPositionHash != info.positionHash) {
    playerOverlayFocused = false;
    itemIndex = -1;
  }
  lastPlayerPositionHash = info.positionHash;
  vector<PGuiElem> lines;
  const int maxElems = 6;
  const string title = "Click or press [Enter]:";
  int numElems = min<int>(maxElems, info.lyingItems.size());
  Vec2 size = Vec2(380, (1 + numElems) * legendLineHeight);
  if (!info.lyingItems.empty()) {
    for (int i : All(info.lyingItems)) {
      size.x = max(size.x, 40 + viewObjectWidth + renderer.getTextLength(info.lyingItems[i].name));
      lines.push_back(gui.leftMargin(14, gui.stack(
            gui.mouseHighlight(gui.highlight(listLineHeight), i, &itemIndex),
            gui.leftMargin(6, getItemLine(info.lyingItems[i],
          [this, i](Rectangle) { callbacks.input({UserInputId::PICK_UP_ITEM, i}); },
          [this, i]() { callbacks.input({UserInputId::PICK_UP_ITEM_MULTI, i}); })))));
    }
  }
  int totalElems = info.lyingItems.size();
  if (itemIndex >= totalElems)
    itemIndex = totalElems - 1;
  PGuiElem content;
  if (totalElems == 1 && !playerOverlayFocused)
    content = gui.stack(
        gui.margin(
          gui.leftMargin(3, gui.label(title, colors[ColorId::YELLOW])),
          gui.scrollable(gui.verticalList(std::move(lines), legendLineHeight), &lyingItemsScroll),
          legendLineHeight, GuiFactory::TOP),
        gui.keyHandler([=] { callbacks.input({UserInputId::PICK_UP_ITEM, 0});}, getConfirmationKeys(), true));
  else
    content = gui.stack(makeVec<PGuiElem>(
          gui.focusable(gui.stack(
              gui.keyHandler([=] { callbacks.input({UserInputId::PICK_UP_ITEM, itemIndex});},
                getConfirmationKeys(), true),
              gui.keyHandler([=] { itemIndex = (itemIndex + 1) % totalElems;
                lyingItemsScroll = getScrollPos(itemIndex, totalElems - 1);},
                {gui.getKey(SDL::SDLK_DOWN), gui.getKey(SDL::SDLK_KP_2)}, true),
              gui.keyHandler([=] { itemIndex = (itemIndex + totalElems - 1) % totalElems;
                lyingItemsScroll = getScrollPos(itemIndex, totalElems - 1); },
                {gui.getKey(SDL::SDLK_UP), gui.getKey(SDL::SDLK_KP_8)}, true)),
            getConfirmationKeys(), {gui.getKey(SDL::SDLK_ESCAPE)}, playerOverlayFocused),
          gui.keyHandler([=] { if (!playerOverlayFocused) itemIndex = 0; }, getConfirmationKeys()),
          gui.keyHandler([=] { itemIndex = -1; }, {gui.getKey(SDL::SDLK_ESCAPE)}),
          gui.margin(
            gui.leftMargin(3, gui.label(title, colors[ColorId::YELLOW])),
            gui.scrollable(gui.verticalList(std::move(lines), legendLineHeight), &lyingItemsScroll),
            legendLineHeight, GuiFactory::TOP)));
  int margin = 14;
  content = gui.stack(
      gui.conditional(gui.stack(gui.fullScreen(gui.darken()), gui.miniWindow()), gui.translucentBackground(),
        [=] { return playerOverlayFocused;}),
      gui.margins(std::move(content), margin, margin, margin, margin));
  ret.push_back({std::move(content), size + Vec2(margin, margin) * 2, OverlayInfo::TOP_RIGHT});
}

static string getActionText(ItemAction a) {
  switch (a) {
    case ItemAction::DROP: return "drop";
    case ItemAction::DROP_MULTI: return "drop some";
    case ItemAction::GIVE: return "give";
    case ItemAction::EQUIP: return "equip";
    case ItemAction::THROW: return "throw";
    case ItemAction::UNEQUIP: return "remove";
    case ItemAction::APPLY: return "apply";
    case ItemAction::REPLACE: return "replace";
    case ItemAction::LOCK: return "lock";
    case ItemAction::UNLOCK: return "unlock";
    case ItemAction::REMOVE: return "remove";
    case ItemAction::CHANGE_NUMBER: return "change number";
  }
}

void GuiBuilder::drawMiniMenu(GuiFactory::ListBuilder elems, bool& exit, Vec2 menuPos, int width) {
  if (elems.isEmpty())
    return;
  int contentHeight = elems.getSize();
  int margin = 15;
  PGuiElem menu = gui.miniWindow(gui.leftMargin(margin, gui.topMargin(margin, elems.buildVerticalList())),
          [&exit] { exit = true; });
  menu->setBounds(Rectangle(menuPos, menuPos + Vec2(width + 2 * margin, contentHeight + 2 * margin)));
  PGuiElem bg = gui.darken();
  bg->setBounds(renderer.getSize());
  while (1) {
    callbacks.refreshScreen();
    bg->render(renderer);
    menu->render(renderer);
    renderer.drawAndClearBuffer();
    Event event;
    while (renderer.pollEvent(event)) {
      gui.propagateEvent(event, {menu.get()});
      if (exit)
        return;
    }
  }

}

optional<ItemAction> GuiBuilder::getItemChoice(const ItemInfo& itemInfo, Vec2 menuPos, bool autoDefault) {
  if (itemInfo.actions.empty())
    return none;
  if (itemInfo.actions.size() == 1 && autoDefault)
    return itemInfo.actions[0];
  renderer.flushEvents(SDL::SDL_KEYDOWN);
  int contentHeight;
  int choice = -1;
  int index = 0;
  disableTooltip = true;
  DestructorFunction dFun([this] { disableTooltip = false; });
  vector<string> options = transform2<string>(itemInfo.actions, bindFunction(getActionText));
  options.push_back("cancel");
  int count = options.size();
  PGuiElem stuff = drawListGui("", ListElem::convert(options), MenuType::NORMAL, &contentHeight, &index, &choice);
  stuff = gui.miniWindow(gui.margins(std::move(stuff), 0));
  Vec2 size(150, options.size() * listLineHeight + 35);
  menuPos.x = min(menuPos.x, renderer.getSize().x - size.x);
  menuPos.y = min(menuPos.y, renderer.getSize().y - size.y);
  while (1) {
    callbacks.refreshScreen();
    stuff->setBounds(Rectangle(menuPos, menuPos + size));
    stuff->render(renderer);
    renderer.drawAndClearBuffer();
    Event event;
    while (renderer.pollEvent(event)) {
      gui.propagateEvent(event, {stuff.get()});
      if (choice > -1 && index > -1) {
        if (index < itemInfo.actions.size())
          return itemInfo.actions[index];
        else
          return none;
      }
      if (choice == -100)
        return none;
      if (event.type == SDL::SDL_MOUSEBUTTONDOWN &&
          !Vec2(event.button.x, event.button.x).inRectangle(stuff->getBounds()))
        return none;
      if (event.type == SDL::SDL_KEYDOWN)
        switch (event.key.keysym.sym) {
          case SDL::SDLK_KP_8:
          case SDL::SDLK_UP: index = (index - 1 + count) % count; break;
          case SDL::SDLK_KP_2:
          case SDL::SDLK_DOWN: index = (index + 1 + count) % count; break;
          case SDL::SDLK_KP_5:
          case SDL::SDLK_KP_ENTER:
          case SDL::SDLK_RETURN:
              if (index > -1) {
                if (index < itemInfo.actions.size())
                  return itemInfo.actions[index];
              }
          case SDL::SDLK_ESCAPE: return none;
          default: break;
        }
    }
  }
}

vector<PGuiElem> GuiBuilder::drawSkillsList(const PlayerInfo& info) {
  vector<PGuiElem> lines;
  if (!info.skills.empty()) {
    lines.push_back(gui.label("Skills", colors[ColorId::YELLOW]));
    for (auto& elem : info.skills)
      lines.push_back(gui.stack(getTooltip({elem.help}),
            gui.label(capitalFirst(elem.name), colors[ColorId::WHITE])));
    lines.push_back(gui.empty());
  }
  return lines;
}

const int spellsPerRow = 5;
const Vec2 spellIconSize = Vec2(47, 47);

PGuiElem GuiBuilder::getSpellIcon(const PlayerInfo::Spell& spell, bool active) {
  vector<PGuiElem> ret = makeVec<PGuiElem>(
      gui.spellIcon(spell.id));
  if (spell.timeout) {
    ret.push_back(gui.darken());
    ret.push_back(gui.centeredLabel(Renderer::HOR_VER, toString(*spell.timeout)));
  } else
  if (active)
    ret.push_back(gui.button(getButtonCallback({UserInputId::CAST_SPELL, spell.id})));
  ret.push_back(getTooltip({spell.name, spell.help}));
  return gui.stack(std::move(ret));
}

vector<PGuiElem> GuiBuilder::drawSpellsList(const PlayerInfo& info, bool active) {
  vector<PGuiElem> list;
  if (!info.spells.empty()) {
    vector<PGuiElem> line;
    for (auto& elem : info.spells) {
      line.push_back(getSpellIcon(elem, active));
      if (line.size() >= spellsPerRow)
        list.push_back(gui.horizontalList(std::move(line), spellIconSize.x));
    }
    if (!line.empty())
      list.push_back(gui.horizontalList(std::move(line), spellIconSize.x));
  }
  return list;
}

vector<PGuiElem> GuiBuilder::drawEffectsList(const PlayerInfo& info) {
  vector<PGuiElem> lines;
  for (auto effect : info.effects)
    lines.push_back(gui.stack(
          getTooltip({effect.help}),
          gui.label(effect.name, effect.bad ? colors[ColorId::RED] : colors[ColorId::WHITE])));
  return lines;
}

static string getKeybindingDesc(char c) {
  if (c == ' ')
    return "[Space]";
  else
    return "["_s + (char)toupper(c) + "]";
}

static vector<string> help = {
    "Move around with number pad.",
    "Extended attack: ctrl + arrow.",
    "Fast travel: ctrl + arrow.",
    "Fire arrows: alt + arrow.",
};

PGuiElem GuiBuilder::getExpIncreaseLine(const PlayerInfo::LevelInfo& info, ExperienceType type) {
  auto line = gui.getListBuilder();
  line.addElemAuto(gui.label(capitalFirst(toLower(EnumInfo<ExperienceType>::getString(type))) + ": "));
  line.addElemAuto(gui.label(toString(info.increases[type])));
  if (auto limit = info.limits[type])
    line.addElemAuto(gui.label("  (limit " + toString(*limit) + ")"));
  return line.buildHorizontalList();
}

PGuiElem GuiBuilder::drawPlayerLevelButton(const PlayerInfo& info) {
  return gui.stack(
      gui.labelHighlight("[Level " + toString(info.levelInfo.level) + "]", colors[ColorId::LIGHT_BLUE]),
      gui.buttonRect([=] (Rectangle bounds) {
          auto lines = gui.getListBuilder(legendLineHeight);
          bool exit = false;
          lines.addElem(gui.label("Level " + toString(info.levelInfo.level)));
          lines.addElem(gui.label("Increases:", colors[ColorId::YELLOW]));
          for (auto expType : ENUM_ALL(ExperienceType))
            lines.addElem(gui.leftMargin(30, getExpIncreaseLine(info.levelInfo, expType)));
          if (auto& warning = info.levelInfo.warning)
            lines.addElem(gui.label(*warning, colors[ColorId::RED]));
          drawMiniMenu(std::move(lines), exit, bounds.bottomLeft(), 300);
      }));
}

PGuiElem GuiBuilder::drawPlayerInventory(PlayerInfo& info) {
  GuiFactory::ListBuilder list(gui, legendLineHeight);
  list.addElem(gui.label(info.getTitle(), colors[ColorId::WHITE]));
  list.addElem(drawPlayerLevelButton(info));
  auto line = gui.getListBuilder();
  vector<PGuiElem> keyElems;
  for (int i : All(info.commands))
    if (info.commands[i].active) {
      keyElems.push_back(gui.keyHandlerChar(getButtonCallback({UserInputId::PLAYER_COMMAND, i}),
          info.commands[i].keybinding));
    }
  line.addElemAuto(gui.stack(
      gui.stack(std::move(keyElems)),
      gui.labelHighlight("[Commands]", colors[ColorId::LIGHT_BLUE]),
      gui.buttonRect([=] (Rectangle bounds) {
          auto lines = gui.getListBuilder(legendLineHeight);
          bool exit = false;
          optional<ItemAction> ret;
          for (auto& elem : help)
            lines.addElem(gui.label(elem, colors[ColorId::LIGHT_BLUE]));
          for (int i : All(info.commands)) {
            auto& command = info.commands[i];
            function<void()> buttonFun = [] {};
            if (command.active)
              buttonFun = [&exit, &ret, i, this] {
                  callbacks.input({UserInputId::PLAYER_COMMAND, i});
                  exit = true;
              };
            auto labelColor = colors[command.active ? ColorId::WHITE : ColorId::GRAY];
            lines.addElem(gui.stack(
                gui.buttonChar(buttonFun, command.keybinding),
                command.active ? gui.uiHighlightMouseOver(colors[ColorId::GREEN]) : gui.empty(),
                gui.tooltip({command.description}),
                gui.label(getKeybindingDesc(command.keybinding) + " " + command.name, labelColor)));
          }
          for (auto& button : getSettingsButtons())
            lines.addElem(std::move(button));
          drawMiniMenu(std::move(lines), exit, bounds.bottomLeft(), 260);
     })));
  line.addElem(gui.button(getButtonCallback(UserInputId::CHEAT_ATTRIBUTES), gui.getKey(SDL::SDLK_y)), 1);
  list.addElem(line.buildHorizontalList());
  for (auto& elem : drawEffectsList(info))
    list.addElem(std::move(elem));
  list.addSpace();
  if (info.team.size() > 1) {
    const int numPerLine = 6;
    auto currentLine = gui.getListBuilder();
    currentLine.addElem(gui.label("Team: ", colors[ColorId::WHITE]), 60);
    for (auto& elem : info.team) {
      currentLine.addElem(gui.stack(
            gui.viewObject(elem.viewId),
            gui.label(toString(elem.expLevel), 12)), 30);
      if (currentLine.getLength() >= numPerLine) {
        list.addElem(currentLine.buildHorizontalList());
        currentLine.clear();
      }
    }
    if (!currentLine.isEmpty())
      list.addElem(currentLine.buildHorizontalList());
    list.addSpace();
  }
  for (auto& elem : drawSkillsList(info))
    list.addElem(std::move(elem));
  vector<PGuiElem> spells = drawSpellsList(info, true);
  if (!spells.empty()) {
    list.addElem(gui.label("Spells", colors[ColorId::YELLOW]));
    for (auto& elem : spells)
      list.addElem(std::move(elem), spellIconSize.y);
    list.addSpace();
  }
  if (!info.inventory.empty()) {
    list.addElem(gui.label("Inventory", colors[ColorId::YELLOW]));
    for (auto& item : info.inventory)
      list.addElem(getItemLine(item, [=](Rectangle butBounds) {
            if (auto choice = getItemChoice(item, butBounds.bottomLeft() + Vec2(50, 0), false))
              callbacks.input({UserInputId::INVENTORY_ITEM, InventoryItemInfo{item.ids, *choice}});}));
  }
  return gui.margins(
      gui.scrollable(list.buildVerticalList(), &inventoryScroll), -5, 0, 0, 0);
}

PGuiElem GuiBuilder::drawRightPlayerInfo(PlayerInfo& info) {
  PGuiElem main = drawPlayerInventory(info);
  return gui.margins(std::move(main), 15, 24, 15, 5);
}

typedef CreatureInfo CreatureInfo;

struct CreatureMapElem {
  ViewId viewId;
  int count;
  CreatureInfo any;
};

static map<string, CreatureMapElem> getCreatureMap(const vector<CreatureInfo>& creatures) {
  map<string, CreatureMapElem> creatureMap;
  for (int i : All(creatures)) {
    auto elem = creatures[i];
    if (!creatureMap.count(elem.stackName)) {
      creatureMap.insert(make_pair(elem.stackName, CreatureMapElem({elem.viewId, 1, elem})));
    } else
      ++creatureMap.at(elem.stackName).count;
  }
  return creatureMap;
}

PGuiElem GuiBuilder::drawMinionAndLevel(ViewId viewId, int level, int iconMult) {
  return gui.stack(makeVec<PGuiElem>(
        gui.viewObject(viewId, iconMult),
        gui.label(toString(level), 12 * iconMult)));
}

PGuiElem GuiBuilder::drawTeams(CollectiveInfo& info) {
  const int elemWidth = 30;
  auto lines = gui.getListBuilder(legendLineHeight);
  for (int i : All(info.teams)) {
    auto& team = info.teams[i];
    const int numPerLine = 8;
    auto teamLine = gui.getListBuilder(legendLineHeight);
    vector<PGuiElem> currentLine;
    for (auto member : team.members) {
      auto& memberInfo = *info.getMinion(member);
      currentLine.push_back(drawMinionAndLevel(memberInfo.viewId, memberInfo.expLevel, 1));
      if (currentLine.size() >= numPerLine)
        teamLine.addElem(gui.horizontalList(std::move(currentLine), elemWidth));
    }
    if (!currentLine.empty())
      teamLine.addElem(gui.horizontalList(std::move(currentLine), elemWidth));
    lines.addElemAuto(gui.stack(makeVec<PGuiElem>(
            gui.mouseOverAction([team, this] { mapGui->highlightTeam(team.members); },
              [team, this] { mapGui->unhighlightTeam(team.members); }),
            gui.button(getButtonCallback({UserInputId::SELECT_TEAM, team.id})),
            gui.uiHighlightConditional([team] () { return team.highlight; }),
            gui.uiHighlightMouseOver(),
            gui.dragListener([this, team](DragContent content) {
                UserInputId id;
                switch (content.getId()) {
                  case DragContentId::CREATURE: id = UserInputId::ADD_TO_TEAM; break;
                  case DragContentId::CREATURE_GROUP: id = UserInputId::ADD_GROUP_TO_TEAM; break;
                }
                callbacks.input({id, TeamCreatureInfo{team.id, content.get<UniqueEntity<Creature>::Id>()}});}),
            gui.getListBuilder(22)
            .addElem(gui.topMargin(8, gui.icon(GuiFactory::TEAM_BUTTON, GuiFactory::Alignment::TOP_CENTER)))
            .addElemAuto(teamLine.buildVerticalList()).buildHorizontalList())));
  }
  string hint = "Drag and drop minions onto the [new team] button to create a new team. "
    "You can drag them both from the map and the menus.";
  lines.addElem(gui.stack(makeVec<PGuiElem>(
        gui.dragListener([this](DragContent content) {
            UserInputId id;
            switch (content.getId()) {
              case DragContentId::CREATURE: id = UserInputId::CREATE_TEAM; break;
              case DragContentId::CREATURE_GROUP: id = UserInputId::CREATE_TEAM_FROM_GROUP; break;
            }
            callbacks.input({id, content.get<UniqueEntity<Creature>::Id>() });}),
        gui.uiHighlightMouseOver(),
        getHintCallback({hint}),
        gui.button([this, hint] { callbacks.info(hint); }),
        gui.label("[new team]", colors[ColorId::WHITE]))));
  return lines.buildVerticalList();
}

vector<PGuiElem> GuiBuilder::getSettingsButtons() {
  return makeVec<PGuiElem>(
      gui.stack(makeVec<PGuiElem>(
            getHintCallback({"Morale affects minion's productivity and chances of fleeing from battle."}),
            gui.uiHighlightConditional([=] { return mapGui->highlightMorale();}),
            gui.label("Highlight morale"),
            gui.button([this] { mapGui->setHighlightMorale(!mapGui->highlightMorale()); }))),
      gui.stack(makeVec<PGuiElem>(
            gui.uiHighlightConditional([=] { return mapGui->highlightEnemies();}),
            gui.label("Highlight enemies"),
            gui.button([this] { mapGui->setHighlightEnemies(!mapGui->highlightEnemies()); }))));
}

PGuiElem GuiBuilder::drawMinions(CollectiveInfo& info) {
  int newHash = info.getHash();
  if (newHash != minionsHash) {
    minionsHash = newHash;
    auto list = gui.getListBuilder(legendLineHeight);
    list.addElem(gui.label(info.monsterHeader, colors[ColorId::WHITE]));
    for (int i : All(info.minionGroups)) {
      auto& elem = info.minionGroups[i];
      auto line = gui.getListBuilder();
      vector<int> widths;
      line.addElem(gui.viewObject(elem.viewId), 40);
      PGuiElem tmp = gui.label(toString(elem.count) + "   " + elem.name, colors[ColorId::WHITE]);
      if (elem.highlight)
        tmp = gui.stack(gui.uiHighlight(), std::move(tmp));
      line.addElem(std::move(tmp), 200);
      list.addElem(gui.leftMargin(20, gui.stack(
          gui.button(getButtonCallback({UserInputId::CREATURE_GROUP_BUTTON, elem.creatureId})),
          gui.dragSource({DragContentId::CREATURE_GROUP, elem.creatureId},
              [=]{ return gui.getListBuilder(10)
                  .addElemAuto(gui.label(toString(elem.count) + " "))
                  .addElem(gui.viewObject(elem.viewId)).buildHorizontalList();}),
          line.buildHorizontalList())));
    }
    list.addElem(gui.label("Teams: ", colors[ColorId::WHITE]));
    list.addElemAuto(drawTeams(info));
    list.addSpace();
    list.addElem(gui.stack(
              gui.uiHighlightConditional([=] { return showTasks;}),
              gui.label("Show tasks"),
              gui.button([this] { closeOverlayWindows(); showTasks = !showTasks; })));
    for (auto& button : getSettingsButtons())
      list.addElem(std::move(button));
    list.addElem(gui.stack(
              gui.label("Show message history"),
              gui.button(getButtonCallback(UserInputId::SHOW_HISTORY))));
    list.addSpace();
    if (!info.enemyGroups.empty()) {
      list.addElem(gui.label("Enemies:", colors[ColorId::WHITE]));
      for (auto& elem : info.enemyGroups){
        vector<PGuiElem> line;
        line.push_back(gui.viewObject(elem.viewId));
        line.push_back(gui.label(toString(elem.count) + "   " + elem.name, colors[ColorId::WHITE]));
        list.addElem(gui.stack(
            gui.button(getButtonCallback({UserInputId::GO_TO_ENEMY, elem.creatureId})),
            gui.uiHighlightMouseOver(),
            gui.horizontalList(std::move(line), 20)));
      }
    }
    minionsCache = gui.scrollable(list.buildVerticalList(), &minionsScroll, &scrollbarsHeld);
  }
  return gui.external(minionsCache.get());
}

const int taskMapWindowWidth = 400;

void GuiBuilder::drawTasksOverlay(vector<OverlayInfo>& ret, CollectiveInfo& info) {
  if (info.taskMap.empty())
    return;
  vector<PGuiElem> lines;
  vector<PGuiElem> freeLines;
  for (auto& elem : info.taskMap) {
    if (elem.creature)
      if (auto minion = info.getMinion(*elem.creature)) {
        lines.push_back(gui.horizontalList(makeVec<PGuiElem>(
                gui.viewObject(minion->viewId),
                gui.label(elem.name, colors[elem.priority ? ColorId::GREEN : ColorId::WHITE])), 35));
        continue;
      }
    freeLines.push_back(gui.horizontalList(makeVec<PGuiElem>(
            gui.empty(),
            gui.label(elem.name, colors[elem.priority ? ColorId::GREEN : ColorId::WHITE])), 35));
  }
  int lineHeight = 25;
  int margin = 20;
  append(lines, std::move(freeLines));
  ret.push_back({gui.conditional(gui.miniWindow(
        gui.margins(gui.scrollable(gui.verticalList(std::move(lines), lineHeight), &tasksScroll, &scrollbarsHeld),
          margin)), [this] { return showTasks; }),
      Vec2(taskMapWindowWidth, info.taskMap.size() * lineHeight + 2 * margin),
      OverlayInfo::TOP_RIGHT});
}

void GuiBuilder::drawRansomOverlay(vector<OverlayInfo>& ret, const CollectiveInfo::Ransom& ransom) {
  GuiFactory::ListBuilder lines(gui, legendLineHeight);
  lines.addElem(gui.label(ransom.attacker + " demand " + toString(ransom.amount.second)
        + " gold for not attacking. Agree?"));
  if (ransom.canAfford)
    lines.addElem(gui.leftMargin(25, gui.stack(
          gui.mouseHighlight2(gui.highlight(legendLineHeight)),
          gui.button(getButtonCallback(UserInputId::PAY_RANSOM)),
          gui.label("Yes"))));
  else
    lines.addElem(gui.leftMargin(25, gui.label("Yes", colors[ColorId::GRAY])));
  lines.addElem(gui.leftMargin(25, gui.stack(
        gui.mouseHighlight2(gui.highlight(legendLineHeight)),
        gui.button(getButtonCallback(UserInputId::IGNORE_RANSOM)),
        gui.label("No"))));
  int margin = 20;
  ret.push_back({gui.miniWindow(
        gui.margins(lines.buildVerticalList(), margin)),
      Vec2(600 + 2 * margin, lines.getSize() + 2 * margin),
      OverlayInfo::TOP_RIGHT});
}

void GuiBuilder::drawWorkshopsOverlay(vector<OverlayInfo>& ret, CollectiveInfo& info) {
  if (info.chosenWorkshop) {
    int newHash = info.getHash();
    if (newHash != workshopsOverlayHash) {
      workshopsOverlayHash = newHash;
      int margin = 20;
      int rightElemMargin = 10;
      Vec2 size(860, 600);
      auto& options = info.chosenWorkshop->options;
      auto& queued = info.chosenWorkshop->queued;
      auto lines = gui.getListBuilder(legendLineHeight);
      lines.addElem(gui.label("Available:", colors[ColorId::YELLOW]));
      for (int i : All(options)) {
        auto& elem = options[i];
        auto line = gui.getListBuilder();
        line.addElem(gui.viewObject(elem.viewId), 35);
        line.addElem(gui.label(elem.name, colors[elem.unavailable ? ColorId::GRAY : ColorId::WHITE]), 10);
        if (elem.number > 1)
          line.addBackElem(gui.label(toString(elem.number) + "x"), 35);
        line.addBackElem(gui.alignment(GuiFactory::Alignment::RIGHT, drawCost(*elem.price)), 80);
        PGuiElem guiElem = line.buildHorizontalList();
        if (elem.unavailable) {
          CHECK(!elem.unavailableReason.empty());
          guiElem = gui.stack(getTooltip({elem.unavailableReason}), std::move(guiElem));
        }
        else
          guiElem = gui.stack(
              getTooltip({elem.description}),
              gui.uiHighlightMouseOver(colors[ColorId::GREEN]),
              std::move(guiElem),
              gui.button(getButtonCallback({UserInputId::WORKSHOP_ADD, i})));
        lines.addElem(gui.rightMargin(rightElemMargin, std::move(guiElem)));
      }
      auto lines2 = gui.getListBuilder(legendLineHeight);
      lines2.addElem(gui.label("In production:", colors[ColorId::YELLOW]));
      for (int i : All(queued)) {
        auto& elem = queued[i];
        auto line = gui.getListBuilder();
        line.addMiddleElem(gui.stack(
            gui.uiHighlightMouseOver(colors[ColorId::GREEN]),
            gui.buttonRect([=] (Rectangle bounds) {
                  auto lines = gui.getListBuilder(legendLineHeight);
                  bool exit = false;
                  optional<ItemAction> ret;
                  for (auto action : elem.actions) {
                    function<void()> buttonFun = [] {};
                    if (!elem.unavailable)
                      buttonFun = [&exit, &ret, action] {
                          ret = action;
                          exit = true;
                      };
                    lines.addElem(gui.stack(
                          gui.button(buttonFun),
                          gui.uiHighlightMouseOver(colors[ColorId::GREEN]),
                          gui.label(getActionText(action))));
                  }
                  drawMiniMenu(std::move(lines), exit, bounds.bottomLeft(), 200);
                  if (ret)
                    callbacks.input({UserInputId::WORKSHOP_ITEM_ACTION,
                        WorkshopQueuedActionInfo{i, *ret}});
            }),
            gui.getListBuilder()
                .addElem(gui.viewObject(elem.viewId), 35)
                .addElemAuto(gui.label(elem.name)).buildHorizontalList()));
        line.addBackElem(gui.stack(
            gui.uiHighlightMouseOver(colors[ColorId::GREEN]),
            gui.button(getButtonCallback({UserInputId::WORKSHOP_ITEM_ACTION,
                WorkshopQueuedActionInfo{i, ItemAction::CHANGE_NUMBER}})),
            gui.label(toString(elem.number) + "x")), 35);
        line.addBackElem(gui.alignment(GuiFactory::Alignment::RIGHT, drawCost(*elem.price)), 80);
        lines2.addElem(gui.stack(
            gui.bottomMargin(5,
                gui.progressBar(transparency(colors[ColorId::DARK_GREEN], 128), elem.productionState)),
            gui.rightMargin(rightElemMargin, line.buildHorizontalList())));
      }
      size.y = min(600, max(lines.getSize(), lines2.getSize()) + 2 * margin);
      workshopsOverlayCache = gui.stack(gui.preferredSize(size.x, size.y),
        gui.miniWindow(gui.stack(
          gui.keyHandler(getButtonCallback({UserInputId::WORKSHOP, info.chosenWorkshop->index}),
            {gui.getKey(SDL::SDLK_ESCAPE)}, true),
          gui.getListBuilder(430)
                .addElem(gui.margins(gui.scrollable(
                      lines.buildVerticalList(), &workshopsScroll, &scrollbarsHeld), margin))
                .addElem(gui.margins(
                    gui.scrollable(lines2.buildVerticalList(), &workshopsScroll2, &scrollbarsHeld),
                    margin)).buildHorizontalList())));
    }
    ret.push_back({gui.external(workshopsOverlayCache.get()),
        Vec2(*workshopsOverlayCache->getPreferredWidth(), *workshopsOverlayCache->getPreferredHeight()),
        OverlayInfo::MINIONS});
  }
}

void GuiBuilder::drawMinionsOverlay(vector<OverlayInfo>& ret, CollectiveInfo& info) {
  int margin = 20;
  Vec2 size(600, 600);
  int minionListWidth = 220;
  size.x += minionListWidth;
  if (info.chosenCreature) {
    int newHash = info.getHash();
    if (newHash != minionsOverlayHash) {
      PGuiElem minionPage;
      auto& minions = info.chosenCreature->creatures;
      auto current = info.chosenCreature->chosenId;
      for (int i : All(minions))
        if (minions[i].creatureId == current)
          minionPage = gui.margins(drawMinionPage(minions[i]), 10, 15, 10, 10);
      if (!minionPage)
        return;
      PGuiElem menu;
      PGuiElem leftSide = drawMinionButtons(minions, current, info.chosenCreature->teamId);
      if (info.chosenCreature->teamId) {
        auto list = gui.getListBuilder(legendLineHeight);
        list.addElem(gui.stack(
            gui.button(getButtonCallback({UserInputId::CANCEL_TEAM, *info.chosenCreature->teamId})),
            gui.labelHighlight("[Disband team]", colors[ColorId::LIGHT_BLUE])));
        list.addElem(gui.label("Control a chosen minion to", Renderer::smallTextSize,
              colors[ColorId::LIGHT_GRAY]), Renderer::smallTextSize + 2);
        list.addElem(gui.label("command the team.", Renderer::smallTextSize,
              colors[ColorId::LIGHT_GRAY]));
        list.addElem(gui.empty(), legendLineHeight);
        leftSide = gui.marginAuto(list.buildVerticalList(), std::move(leftSide), GuiFactory::TOP);
      }
      menu = gui.stack(
          gui.horizontalList(makeVec<PGuiElem>(
              gui.margins(std::move(leftSide), 8, 15, 5, 0),
              gui.margins(gui.sprite(GuiFactory::TexId::VERT_BAR_MINI, GuiFactory::Alignment::LEFT),
                0, -15, 0, -15)), minionListWidth),
          gui.leftMargin(minionListWidth + 20, std::move(minionPage)));
      minionsOverlayCache = gui.miniWindow(gui.stack(
          gui.keyHandler(getButtonCallback({UserInputId::CREATURE_BUTTON, UniqueEntity<Creature>::Id()}),
            {gui.getKey(SDL::SDLK_ESCAPE)}, true),
          gui.margins(std::move(menu), margin)));
      minionsOverlayHash = newHash;
    }
    ret.push_back({gui.external(minionsOverlayCache.get()), size, OverlayInfo::MINIONS});
  }
}

void GuiBuilder::drawBuildingsOverlay(vector<OverlayInfo>& ret, CollectiveInfo& info) {
  int hash = combineHash(info.buildings);
  if (buildingsOverlayHash != hash) {
    buildingsOverlayHash = hash;
    buildingsOverlayCache.clear();
    map<string, GuiFactory::ListBuilder> overlaysMap;
    int margin = 20;
    for (int i : All(info.buildings)) {
      auto& elem = info.buildings[i];
      if (!elem.groupName.empty()) {
        if (!overlaysMap.count(elem.groupName))
          overlaysMap.emplace(make_pair(elem.groupName, gui.getListBuilder(legendLineHeight)));
        overlaysMap.at(elem.groupName).addElem(getButtonLine(elem, i, CollectiveTab::BUILDINGS));
        buildingsOverlayCache.push_back({gui.conditional(
              gui.miniWindow(gui.margins(getButtonLine(elem, i, CollectiveTab::BUILDINGS), margin)),
              [i, this] { return getActiveButton(CollectiveTab::BUILDINGS) == i;}),
            Vec2(300, legendLineHeight + 2 * margin), OverlayInfo::TOP_RIGHT});
      }
    }
    for (auto& elem : overlaysMap) {
      auto& lines = elem.second;
      lines.addElem(gui.stack(
            gui.centeredLabel(Renderer::HOR, "[close]", colors[ColorId::LIGHT_BLUE]),
            gui.button([=] { clearActiveButton(); })), legendLineHeight);
      int height = lines.getSize() - 8;
      string groupName = elem.first;
      buildingsOverlayCache.push_back({gui.conditionalStopKeys(
            gui.miniWindow(gui.stack(
                gui.keyHandler([=] { clearActiveButton(); }, {gui.getKey(SDL::SDLK_ESCAPE)}, true),
                gui.margins(lines.buildVerticalList(), margin))),
            [=] { return !info.ransom && collectiveTab == CollectiveTab::BUILDINGS &&
            activeGroup == groupName;}),
          Vec2(300 + 2 * margin, height + 2 * margin),
          OverlayInfo::TOP_RIGHT});
    }
  }
  for (auto& elem : buildingsOverlayCache)
    ret.push_back({gui.external(elem.elem.get()), elem.size, elem.alignment});
}

void GuiBuilder::drawBandOverlay(vector<OverlayInfo>& ret, CollectiveInfo& info) {
  if (info.ransom)
    drawRansomOverlay(ret, *info.ransom);
  drawMinionsOverlay(ret, info);
  drawWorkshopsOverlay(ret, info);
  drawTasksOverlay(ret, info);
  drawBuildingsOverlay(ret, info);
}

int GuiBuilder::getNumMessageLines() const {
  return 3;
}

static Color makeBlack(const Color& col, double freshness) {
  double amount = 0.5 + freshness / 2;
  return Color(col.r * amount, col.g * amount, col.b * amount);
}

static Color getMessageColor(MessagePriority priority) {
  switch (priority) {
    case MessagePriority::NORMAL: return colors[ColorId::WHITE];
    case MessagePriority::HIGH: return colors[ColorId::ORANGE];
    case MessagePriority::CRITICAL: return colors[ColorId::RED];
  }
}

static Color getMessageColor(const PlayerMessage& msg) {
  return makeBlack(getMessageColor(msg.getPriority()), msg.getFreshness());
}

const int messageArrowLength = 15;

static int getMessageLength(Renderer& renderer, const PlayerMessage& msg) {
  return renderer.getTextLength(msg.getText()) + (msg.isClickable() ? messageArrowLength : 0);
}

static int getNumFitting(Renderer& renderer, const vector<PlayerMessage>& messages, int maxLength, int numLines) {
  int currentWidth = 0;
  int currentLine = 0;
  for (int i = messages.size() - 1; i >= 0; --i) {
    int length = getMessageLength(renderer, messages[i]);
    length = min(length, maxLength);
    if (currentWidth > 0)
      ++length;
    if (currentWidth + length > maxLength) {
      if (currentLine == numLines - 1)
        return messages.size() - i - 1;
      currentWidth = 0;
      ++currentLine;
    }
    currentWidth += length;
  }
  return messages.size();
}

static vector<vector<PlayerMessage>> fitMessages(Renderer& renderer, const vector<PlayerMessage>& messages,
    int maxLength, int numLines) {
  int currentWidth = 0;
  vector<vector<PlayerMessage>> ret {{}};
  int numFitting = getNumFitting(renderer, messages, maxLength, numLines);
  for (int i : Range(messages.size() - numFitting, messages.size())) {
    int length = getMessageLength(renderer, messages[i]);
    length = min(length, maxLength);
    if (currentWidth > 0)
      ++length;
    if (currentWidth + length > maxLength) {
      if (ret.size() == numLines)
        break;
      currentWidth = 0;
      ret.emplace_back();
    }
    currentWidth += length;
    ret.back().push_back(messages[i]);
  }
  return ret;
}

static void cutToFit(Renderer& renderer, string& s, int maxLength) {
  while (!s.empty() && renderer.getTextLength(s) > maxLength)
    s.pop_back();
}

void GuiBuilder::drawMessages(vector<OverlayInfo>& ret,
    const vector<PlayerMessage>& messageBuffer, int maxMessageLength) {
  int hMargin = 10;
  int vMargin = 5;
  vector<vector<PlayerMessage>> messages = fitMessages(renderer, messageBuffer, maxMessageLength - 2 * hMargin,
      getNumMessageLines());
  int lineHeight = 20;
  vector<PGuiElem> lines;
  for (int i : All(messages)) {
    GuiFactory::ListBuilder line(gui);
    for (auto& message : messages[i]) {
      string text = (line.isEmpty() ? "" : " ") + message.getText();
      cutToFit(renderer, text, maxMessageLength - 2 * hMargin);
      if (message.isClickable()) {
        line.addElemAuto(gui.stack(
              gui.button(getButtonCallback(UserInput(UserInputId::MESSAGE_INFO, message.getUniqueId()))),
              gui.labelHighlight(text, getMessageColor(message))));
        line.addElemAuto(gui.labelUnicode(u8"➚", getMessageColor(message)));
      } else
      line.addElemAuto(gui.stack(
            gui.button(getButtonCallback(UserInput(UserInputId::MESSAGE_INFO, message.getUniqueId()))),
            gui.label(text, getMessageColor(message))));
    }
    if (!messages[i].empty())
      lines.push_back(line.buildHorizontalList());
  }
  if (!lines.empty())
    ret.push_back({gui.translucentBackground(
        gui.margins(gui.verticalList(std::move(lines), lineHeight), hMargin, vMargin, hMargin, vMargin)),
        Vec2(maxMessageLength, lineHeight * messages.size() + 15), OverlayInfo::MESSAGES});
}

PGuiElem GuiBuilder::getVillageStateLabel(VillageInfo::Village::State state) {
  switch (state) {
    case VillageInfo::Village::FRIENDLY: return gui.label("Friendly", colors[ColorId::GREEN]);
    case VillageInfo::Village::HOSTILE: return gui.label("Hostile", colors[ColorId::ORANGE]);
    case VillageInfo::Village::CONQUERED: return gui.label("Conquered", colors[ColorId::LIGHT_BLUE]);
  }
}

static const char* getVillageActionText(VillageAction action) {
  switch (action) {
    case VillageAction::RECRUIT:
      return "Recruit";
    case VillageAction::TRADE:
      return "Trade";
    case VillageAction::PILLAGE:
      return "Pillage";
  }
}

PGuiElem GuiBuilder::getVillageActionButton(int villageIndex, VillageInfo::Village::ActionInfo action) {
  if (action.disabledReason)
    return gui.stack(
        gui.label(getVillageActionText(action.action), colors[ColorId::GRAY]),
        getTooltip({*action.disabledReason}));
  else
    return gui.stack(
        gui.labelHighlight(getVillageActionText(action.action), colors[ColorId::GREEN]),
        gui.button(getButtonCallback({UserInputId::VILLAGE_ACTION,
            VillageActionInfo{villageIndex, action.action}})));
}

static Color getTriggerColor(double value) {
  return Color::f(1, max(0.0, 1 - value * 500), max(0.0, 1 - value * 500));
}

void GuiBuilder::showAttackTriggers(const vector<VillageInfo::Village::TriggerInfo>& triggers, Vec2 pos) {
  vector<PGuiElem> elems;
  for (auto& trigger : triggers)
#ifdef RELEASE
    if (trigger.value > 0)
#endif
    elems.push_back(gui.label(trigger.name
#ifndef RELEASE
          + " " + toString(trigger.value)
#endif
          ,getTriggerColor(trigger.value)));
  bool exit = false;
  if (!elems.empty()) {
    auto list = gui.getListBuilder(legendLineHeight);
    list.addElem(gui.label("Potential attack triggers:"), titleLineHeight);
    for (auto& elem : elems)
      list.addElem(std::move(elem));
    drawMiniMenu(std::move(list), exit, pos, 300);
  }
}

PGuiElem GuiBuilder::drawVillages(VillageInfo& info) {
  int currentHash = combineHash(info);
  if (currentHash != villagesHash) {
    villagesHash = currentHash;
    auto lines = gui.getListBuilder(legendLineHeight);
    int titleMargin = -11;
    lines.addElem(gui.leftMargin(titleMargin, gui.label(toString(info.numConquered) + "/" +
            toString(info.totalMain) + " main villains conquered.")), titleLineHeight);
    if (info.numMainVillains > 0)
      lines.addElem(gui.leftMargin(titleMargin, gui.label("Main villains:")), titleLineHeight);
    for (int i : All(info.villages)) {
      if (i == info.numMainVillains && info.numLesserVillains > 0) {
        lines.addSpace();
        lines.addElem(gui.leftMargin(titleMargin, gui.label("Lesser villains:")), titleLineHeight);
      }
      if (i == info.numMainVillains + info.numLesserVillains) {
        lines.addSpace();
        lines.addElem(gui.leftMargin(titleMargin, gui.label("Allies:")), titleLineHeight);
      }
      auto& elem = info.villages[i];
      string title = capitalFirst(elem.name) + (elem.tribeName.empty() ?
            string() : " (" + elem.tribeName + ")");
      PGuiElem header;
      if (info.villages[i].access == VillageInfo::Village::LOCATION)
        header = gui.stack(gui.button(getButtonCallback({UserInputId::GO_TO_VILLAGE, i})),
          gui.getListBuilder()
              .addElemAuto(gui.labelHighlight(title))
              .addSpace(7)
              .addElemAuto(gui.labelUnicode(u8"➚")).buildHorizontalList());
      else
        header = gui.label(title);
      lines.addElem(std::move(header));
      if (info.villages[i].access == VillageInfo::Village::NO_LOCATION)
        lines.addElem(gui.leftMargin(40, gui.label("Location unknown", colors[ColorId::LIGHT_BLUE])));
      else if (info.villages[i].access == VillageInfo::Village::INACTIVE)
        lines.addElem(gui.leftMargin(40, gui.label("Outside influence zone", colors[ColorId::GRAY])));
      GuiFactory::ListBuilder line(gui);
      line.addElemAuto(gui.margins(getVillageStateLabel(elem.state), 40, 0, 40, 0));
      vector<VillageInfo::Village::TriggerInfo> triggers = elem.triggers;
      sort(triggers.begin(), triggers.end(),
          [] (const VillageInfo::Village::TriggerInfo& t1, const VillageInfo::Village::TriggerInfo& t2) {
              return t1.value > t2.value;});
  #ifdef RELEASE
      triggers = filter(triggers, [](const VillageInfo::Village::TriggerInfo& t) { return t.value > 0;});
  #endif
      if (!triggers.empty())
        line.addElemAuto(gui.stack(
            gui.labelHighlight("Triggers", colors[ColorId::RED]),
            gui.buttonRect([this, triggers](Rectangle bounds) {
                showAttackTriggers(triggers, bounds.topRight() + Vec2(20, 0));})));
      lines.addElem(line.buildHorizontalList());
      for (auto action : elem.actions)
        lines.addElem(gui.margins(getVillageActionButton(i, action), 40, 0, 0, 0));

    }
    if (lines.isEmpty())
      return gui.label("No foreign tribes discovered.");
    int numVillains = info.villages.size();
    if (numSeenVillains == -1)
      numSeenVillains = numVillains;
    villagesCache = gui.stack(
      gui.onRenderedAction([this, numVillains] { numSeenVillains = numVillains;}),
      gui.scrollable(lines.buildVerticalList(), &villagesScroll, &scrollbarsHeld));
  }
  return gui.external(villagesCache.get());
}

const double menuLabelVPadding = 0.15;

Rectangle GuiBuilder::getMinionMenuPosition() {
  Vec2 center = renderer.getSize() / 2;
  Vec2 dim(800, 600);
  return Rectangle(center - dim / 2, center + dim / 2);
}

Rectangle GuiBuilder::getEquipmentMenuPosition(int height) {
  Rectangle minionMenu = getMinionMenuPosition();
  int width = 340;
  Vec2 origin = minionMenu.topRight() + Vec2(-100, 200);
  origin.x = min(origin.x, renderer.getSize().x - width);
  return Rectangle(origin, origin + Vec2(width, height)).intersection(Rectangle(Vec2(0, 0), renderer.getSize()));
}

Rectangle GuiBuilder::getMenuPosition(MenuType type, int numElems) {
  int windowWidth = 800;
  int windowHeight = 400;
  int ySpacing;
  int yOffset = 0;
  switch (type) {
    case MenuType::YES_NO:
      ySpacing = (renderer.getSize().y - 250) / 2;
      break;
    case MenuType::MAIN_NO_TILES:
      ySpacing = (renderer.getSize().y - windowHeight) / 2;
      break;
    case MenuType::MAIN:
      windowWidth = 0.41 * renderer.getSize().y;
      ySpacing = (1.0 + (5 - numElems) * 0.1) * renderer.getSize().y / 3;
      break;
    case MenuType::GAME_CHOICE:
      windowWidth = 0.41 * renderer.getSize().y;
      ySpacing = renderer.getSize().y * 0.28;
      yOffset = renderer.getSize().y * 0.05;
      break;
    default: ySpacing = 100; break;
  }
  int xSpacing = (renderer.getSize().x - windowWidth) / 2;
  return Rectangle(xSpacing, ySpacing + yOffset, xSpacing + windowWidth, renderer.getSize().y - ySpacing + yOffset);
}

vector<PGuiElem> GuiBuilder::getMultiLine(const string& text, Color color, MenuType menuType, int maxWidth) {
  vector<PGuiElem> ret;
  for (const string& s : gui.breakText(text, maxWidth)) {
    if (menuType != MenuType::MAIN)
      ret.push_back(gui.label(s, color));
    else
      ret.push_back(gui.mainMenuLabelBg(s, menuLabelVPadding));

  }
  return ret;
}

PGuiElem GuiBuilder::menuElemMargins(PGuiElem elem) {
  return gui.margins(std::move(elem), 10, 3, 10, 0);
}

PGuiElem GuiBuilder::getHighlight(MenuType type, const string& label, int height) {
  switch (type) {
    case MenuType::MAIN: return menuElemMargins(gui.mainMenuLabel(label, menuLabelVPadding));
    default: return gui.highlight(height);
  }
}

PGuiElem GuiBuilder::drawListGui(const string& title, const vector<ListElem>& options,
    MenuType menuType, int* height, int* highlight, int* choice) {
  auto lines = gui.getListBuilder(listLineHeight);
  int leftMargin = 30;
  if (!title.empty()) {
    lines.addElem(gui.leftMargin(leftMargin, gui.label(capitalFirst(title), colors[ColorId::WHITE])));
    lines.addSpace();
  } else
    lines.addSpace(listLineHeight / 2);
  int numActive = 0;
  int secColumnWidth = 0;
  int columnWidth = 300;
  for (auto& elem : options) {
    columnWidth = max(columnWidth, renderer.getTextLength(elem.getText()) + 50);
    if (!elem.getSecondColumn().empty())
      secColumnWidth = max(secColumnWidth, 80 + renderer.getTextLength(elem.getSecondColumn()));
  }
  columnWidth = min(columnWidth, getMenuPosition(menuType, options.size()).width() - secColumnWidth - 140);
  if (menuType == MenuType::MAIN)
    columnWidth = 1000000;
  for (int i : All(options)) {
    Color color;
    switch (options[i].getMod()) {
      case ListElem::TITLE: color = gui.titleText; break;
      case ListElem::INACTIVE: color = gui.inactiveText; break;
      case ListElem::TEXT:
      case ListElem::NORMAL: color = gui.text; break;
    }
    if (auto p = options[i].getMessagePriority())
      color = getMessageColor(*p);
    vector<PGuiElem> label1 = getMultiLine(options[i].getText(), color, menuType, columnWidth);
    if (options.size() == 1 && label1.size() > 1) { // hacky way of checking that we display a wall of text
      for (auto& line : label1)
        lines.addElem(gui.leftMargin(leftMargin, std::move(line)));
      break;
    }
    PGuiElem line;
    if (menuType != MenuType::MAIN)
      line = gui.verticalList(std::move(label1), listBrokenLineHeight);
    else
      line = std::move(getOnlyElement(label1));
    if (!options[i].getTip().empty())
      line = gui.stack(std::move(line),
          gui.tooltip({options[i].getTip()}));
    if (!options[i].getSecondColumn().empty())
      line = gui.horizontalList(makeVec<PGuiElem>(std::move(line),
            gui.label(options[i].getSecondColumn())), columnWidth + 80);
    line = menuElemMargins(std::move(line));
    if (highlight && options[i].getMod() == ListElem::NORMAL) {
      line = gui.stack(makeVec<PGuiElem>(
          gui.button([=]() { *choice = numActive; }),
          std::move(line),
          gui.mouseHighlight(getHighlight(menuType, options[i].getText(), listLineHeight), numActive, highlight)));
      ++numActive;
    }
    line = gui.margins(std::move(line), leftMargin, 0, 0, 0);
    lines.addElemAuto(std::move(line));
  }
  if (menuType != MenuType::MAIN)
    return lines.buildVerticalList();
  else
    return lines.buildVerticalListFit();
}

static optional<GuiFactory::IconId> getMoraleIcon(double morale) {
  if (morale >= 0.7)
    return GuiFactory::MORALE_4;
  if (morale >= 0.2)
    return GuiFactory::MORALE_3;
  if (morale < -0.7)
    return GuiFactory::MORALE_1;
  if (morale < -0.2)
    return GuiFactory::MORALE_2;
  else
    return none;
}

static map<ViewId, vector<PlayerInfo>> groupByViewId(const vector<PlayerInfo>& minions) {
  map<ViewId, vector<PlayerInfo>> ret;
  for (auto& elem : minions)
    ret[elem.viewId].push_back(elem);
  return ret;
}

PGuiElem GuiBuilder::drawMinionButtons(const vector<PlayerInfo>& minions, UniqueEntity<Creature>::Id current,
    optional<TeamId> teamId) {
  CHECK(!minions.empty());
  int cache = combineHash(minions, current);
  if (cache != minionButtonsHash) {
    map<ViewId, vector<PlayerInfo>> minionMap = groupByViewId(minions);
    auto list = gui.getListBuilder(legendLineHeight);
    for (auto& elem : minionMap) {
      list.addElem(gui.topMargin(5, gui.viewObject(elem.first)), legendLineHeight + 5);
      for (auto& minion : elem.second) {
        auto minionId = minion.creatureId;
        GuiFactory::ListBuilder line(gui);
        if (teamId)
          line.addElem(gui.leftMargin(-16, gui.stack(
              gui.button(getButtonCallback({UserInputId::REMOVE_FROM_TEAM, TeamCreatureInfo{*teamId, minionId}})),
              gui.labelUnicode(u8"✘", colors[ColorId::RED]))), 1);
        line.addElemAuto(gui.rightMargin(5, gui.label(minion.getFirstName())));
        if (auto icon = getMoraleIcon(minion.morale))
          line.addElem(gui.topMargin(-2, gui.icon(*icon)), 20);
        line.addBackElem(gui.label("L:" + toString<int>(minion.levelInfo.level)), 42);
        list.addElem(gui.stack(makeVec<PGuiElem>(
              gui.button(getButtonCallback({UserInputId::CREATURE_BUTTON, minionId})),
              gui.uiHighlight([=] { return mapGui->getCreatureHighlight(minionId);}),
              gui.uiHighlightConditional([=] { return current == minionId;}),
              gui.dragSource({DragContentId::CREATURE, minionId},
                [=]{ return gui.viewObject(minion.viewId);}),
              line.buildHorizontalList())));
      }
    }
    minionButtonsCache = gui.scrollable(list.buildVerticalList(), &minionButtonsScroll, &scrollbarsHeld);
    cache = minionButtonsHash;
  }
  return gui.external(minionButtonsCache.get());
}

static string getTaskText(MinionTask option) {
  switch (option) {
    case MinionTask::GRAVE:
    case MinionTask::LAIR:
    case MinionTask::SLEEP: return "Sleeping";
    case MinionTask::EAT: return "Eating";
    case MinionTask::EXPLORE_NOCTURNAL: return "Exploring (night)";
    case MinionTask::EXPLORE_CAVES: return "Exploring caves";
    case MinionTask::EXPLORE: return "Exploring";
    case MinionTask::RITUAL: return "Rituals";
    case MinionTask::CROPS: return "Crops";
    case MinionTask::PRISON: return "Prison";
    case MinionTask::EXECUTE: return "Execution";
    case MinionTask::TRAIN: return "Training";
    case MinionTask::CRAFT: return "Crafting";
    case MinionTask::STUDY: return "Studying";
    case MinionTask::COPULATE: return "Copulating";
    case MinionTask::SPIDER: return "Spider";
    case MinionTask::THRONE: return "Throne";
    case MinionTask::BE_WHIPPED: return "Being whipped";
    case MinionTask::BE_TORTURED: return "Being tortured";
  }
}

static ColorId getTaskColor(PlayerInfo::MinionTaskInfo info) {
  if (info.inactive)
    return ColorId::GRAY;
  else
    return ColorId::WHITE;
}

vector<PGuiElem> GuiBuilder::drawItemMenu(const vector<ItemInfo>& items, ItemMenuCallback callback,
    bool doneBut) {
  vector<PGuiElem> lines;
  for (int i : All(items))
    lines.push_back(getItemLine(items[i], [=] (Rectangle bounds) { callback(bounds, i);} ));
  if (doneBut)
    lines.push_back(gui.stack(
          gui.button([=] { callback(Rectangle(), none); }, gui.getKey(SDL::SDLK_ESCAPE)),
          gui.centeredLabel(Renderer::HOR, "[done]", colors[ColorId::LIGHT_BLUE])));
  return lines;
}

PGuiElem GuiBuilder::drawActivityButton(const PlayerInfo& minion) {
  string curTask = "(none)";
  for (auto task : minion.minionTasks)
    if (task.current)
      curTask = getTaskText(task.task);
  return gui.stack(
      gui.horizontalList(makeVec<PGuiElem>(
          gui.labelHighlight(curTask), gui.labelHighlight("[change]", colors[ColorId::LIGHT_BLUE])),
        renderer.getTextLength(curTask) + 20),
      gui.buttonRect([=] (Rectangle bounds) {
          auto tasks = gui.getListBuilder(legendLineHeight);
          bool exit = false;
          TaskActionInfo retAction;
          retAction.creature = minion.creatureId;
          for (auto task : minion.minionTasks) {
            function<void()> buttonFun = [] {};
            if (!task.inactive)
              buttonFun = [&exit, &retAction, task] {
                  retAction.switchTo = task.task;
                  exit = true;
                };
            tasks.addElem(GuiFactory::ListBuilder(gui)
                .addMiddleElem(gui.stack(
                    gui.button(buttonFun),
                    gui.uiHighlightConditional([=]{ return task.current; }),
                    gui.label(getTaskText(task.task), colors[getTaskColor(task)])))
                .addBackElemAuto(gui.stack(
                    getTooltip({"Click to turn this task on/off."}),
                    gui.button([&exit, &retAction, task] {
                      retAction.lock.toggle(task.task);
                    }),
                    gui.rightMargin(20, gui.labelUnicode(u8"✓", [&retAction, task] {
                        return colors[(retAction.lock.contains(task.task) ^ task.locked) ?
                            ColorId::LIGHT_GRAY : ColorId::GREEN];})))).buildHorizontalList());
          }
          drawMiniMenu(std::move(tasks), exit, bounds.bottomLeft(), 200);
          callbacks.input({UserInputId::CREATURE_TASK_ACTION, retAction});
        }));
}

vector<PGuiElem> GuiBuilder::drawAttributesOnPage(vector<PGuiElem>&& attrs) {
  vector<PGuiElem> lines[2];
  for (int i : All(attrs))
    lines[i % 2].push_back(std::move(attrs[i]));
  int elemWidth = 80;
  return makeVec<PGuiElem>(
        gui.horizontalList(std::move(lines[0]), elemWidth),
        gui.horizontalList(std::move(lines[1]), elemWidth));
}

vector<PGuiElem> GuiBuilder::drawEquipmentAndConsumables(const PlayerInfo& minion) {
  const vector<ItemInfo>& items = minion.inventory;
  if (items.empty())
    return {};
  vector<PGuiElem> lines;
  vector<PGuiElem> itemElems = drawItemMenu(items,
      [=](Rectangle butBounds, optional<int> index) {
        const ItemInfo& item = items[*index];
        if (auto choice = getItemChoice(item, butBounds.bottomLeft() + Vec2(50, 0), true))
          callbacks.input({UserInputId::CREATURE_EQUIPMENT_ACTION,
              EquipmentActionInfo{minion.creatureId, item.ids, item.slot, *choice}});
      });
  lines.push_back(gui.label("Equipment", colors[ColorId::YELLOW]));
  for (int i : All(itemElems))
    if (items[i].type == items[i].EQUIPMENT)
      lines.push_back(gui.leftMargin(3, std::move(itemElems[i])));
  lines.push_back(gui.label("Consumables", colors[ColorId::YELLOW]));
  for (int i : All(itemElems))
    if (items[i].type == items[i].CONSUMABLE)
      lines.push_back(gui.leftMargin(3, std::move(itemElems[i])));
  lines.push_back(gui.stack(
      gui.labelHighlight("[Add consumable]", colors[ColorId::LIGHT_BLUE]),
      gui.button(getButtonCallback({UserInputId::CREATURE_EQUIPMENT_ACTION,
              EquipmentActionInfo{minion.creatureId, {}, none, ItemAction::REPLACE}}))));
  for (int i : All(itemElems))
    if (items[i].type == items[i].OTHER) {
      lines.push_back(gui.label("Other", colors[ColorId::YELLOW]));
      break;
    }
  for (int i : All(itemElems))
    if (items[i].type == items[i].OTHER)
      lines.push_back(gui.leftMargin(3, std::move(itemElems[i])));
  return lines;
}

vector<PGuiElem> GuiBuilder::drawMinionActions(const PlayerInfo& minion) {
  vector<PGuiElem> line;
  for (auto action : minion.actions)
    switch (action) {
      case PlayerInfo::CONTROL:
        line.push_back(gui.stack(
            gui.labelHighlight("[Control]", colors[ColorId::LIGHT_BLUE]),
            gui.button(getButtonCallback({UserInputId::CREATURE_CONTROL, minion.creatureId}))));
        break;
      case PlayerInfo::RENAME:
        line.push_back(gui.stack(
            gui.labelHighlight("[Rename]", colors[ColorId::LIGHT_BLUE]),
            gui.button([=] {
                if (auto name = getTextInput("Rename minion", minion.firstName, 10, "Press escape to cancel."))
                callbacks.input({UserInputId::CREATURE_RENAME,
                    RenameActionInfo{minion.creatureId, *name}}); })));
        break;
      case PlayerInfo::BANISH:
        line.push_back(gui.stack(
            gui.labelHighlight("[Banish]", colors[ColorId::LIGHT_BLUE]),
            gui.button(getButtonCallback({UserInputId::CREATURE_BANISH, minion.creatureId}))));
        break;
      case PlayerInfo::EXECUTE:
        line.push_back(gui.stack(
            gui.labelHighlight("[Execute]", colors[ColorId::LIGHT_BLUE]),
            gui.button(getButtonCallback({UserInputId::CREATURE_EXECUTE, minion.creatureId}))));
        break;
      case PlayerInfo::CONSUME:
        line.push_back(gui.stack(
            gui.labelHighlight("[Absorb]", colors[ColorId::LIGHT_BLUE]),
            gui.button(getButtonCallback({UserInputId::CREATURE_CONSUME, minion.creatureId}))));
        break;
    }
  return line;
}

vector<PGuiElem> GuiBuilder::joinLists(vector<PGuiElem>&& v1, vector<PGuiElem>&& v2) {
  vector<PGuiElem> ret;
  for (int i : Range(max(v1.size(), v2.size())))
    ret.push_back(gui.horizontalListFit(makeVec<PGuiElem>(
            i < v1.size() ? std::move(v1[i]) : gui.empty(),
            i < v2.size() ? gui.leftMargin(10, std::move(v2[i])) : gui.empty())));
  return ret;
}

PGuiElem GuiBuilder::drawMinionPage(const PlayerInfo& minion) {
  GuiFactory::ListBuilder list(gui, legendLineHeight);
  list.addElem(gui.label(minion.getTitle()));
  if (!minion.description.empty())
    list.addElem(gui.label(minion.description, Renderer::smallTextSize, colors[ColorId::LIGHT_GRAY]));
  list.addElem(gui.horizontalList(drawMinionActions(minion), 140));
  vector<PGuiElem> leftLines;
  leftLines.push_back(gui.label("Attributes", colors[ColorId::YELLOW]));
  leftLines.push_back(drawPlayerLevelButton(minion));
  for (auto& elem : drawAttributesOnPage(drawPlayerAttributes(minion.attributes)))
    leftLines.push_back(std::move(elem));
  for (auto& elem : drawEffectsList(minion))
    leftLines.push_back(std::move(elem));
  leftLines.push_back(gui.empty());
  leftLines.push_back(gui.label("Activity", colors[ColorId::YELLOW]));
  leftLines.push_back(drawActivityButton(minion));
  leftLines.push_back(gui.empty());
  for (auto& elem : drawSkillsList(minion))
    leftLines.push_back(std::move(elem));
  vector<PGuiElem> spells = drawSpellsList(minion, false);
  if (!spells.empty()) {
    leftLines.push_back(gui.label("Spells", colors[ColorId::YELLOW]));
    for (auto& elem : spells) {
      leftLines.push_back(std::move(elem));
      leftLines.push_back(gui.empty());
    }
  }
  int topMargin = list.getSize() + 20;
  return gui.margin(list.buildVerticalList(),
      gui.scrollable(gui.verticalList(joinLists(
          std::move(leftLines),
          drawEquipmentAndConsumables(minion)), legendLineHeight), &minionPageScroll, &scrollbarsHeld),
      topMargin, GuiFactory::TOP);
}

static vector<CreatureMapElem> getRecruitStacks(const vector<CreatureInfo>& creatures) {
  map<string, CreatureMapElem> creatureMap;
  for (int i : All(creatures)) {
    auto elem = creatures[i];
    string key = elem.stackName + " " + toString(elem.cost->second) + " " + toString(elem.expLevel);
    if (!creatureMap.count(key)) {
      creatureMap.insert(make_pair(key, CreatureMapElem({elem.viewId, 1, elem})));
    } else
      ++creatureMap.at(key).count;
  }
  return getValues(creatureMap);
}

PGuiElem GuiBuilder::drawTradeItemMenu(SyncQueue<optional<UniqueEntity<Item>::Id>>& queue, const string& title,
    pair<ViewId, int> budget, const vector<ItemInfo>& items, double* scrollPos) {
  int titleExtraSpace = 10;
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  lines.addElem(GuiFactory::ListBuilder(gui)
      .addElemAuto(gui.label(title))
      .addBackElemAuto(drawCost(budget)).buildHorizontalList(),
     getStandardLineHeight() + titleExtraSpace);
  for (PGuiElem& elem : drawItemMenu(items,
        [&queue, items] (Rectangle, optional<int> index) { if (index) queue.push(*items[*index].ids.begin());}))
    lines.addElem(std::move(elem));
  int menuHeight = lines.getSize() + 30;
  return gui.stack(
      gui.preferredSize(380, menuHeight),
      gui.miniWindow(gui.margins(gui.scrollable(lines.buildVerticalList(), scrollPos), 15),
          [&queue] { queue.push(none); }));
}

PGuiElem GuiBuilder::drawPillageItemMenu(SyncQueue<optional<int>>& queue, const string& title,
    const vector<ItemInfo>& items, double* scrollPos) {
  int titleExtraSpace = 10;
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  lines.addElem(gui.label(title), getStandardLineHeight() + titleExtraSpace);
  for (PGuiElem& elem : drawItemMenu(items,
        [&queue, &items] (Rectangle, optional<int> index) {
            if (index && !items[*index].unavailable) queue.push(*index);}))
    lines.addElem(std::move(elem));
  int menuHeight = lines.getSize() + 30;
  return gui.stack(
      gui.preferredSize(380, menuHeight),
      gui.miniWindow(gui.margins(gui.scrollable(lines.buildVerticalList(), scrollPos), 15),
                     [&queue] { queue.push(none); }));
}

PGuiElem GuiBuilder::drawCampaignGrid(const Campaign& c, optional<Vec2>* marked, function<bool(Vec2)> activeFun,
    function<void(Vec2)> clickFun){
  int iconScale = 2;
  int iconSize = 24 * iconScale;;
  auto rows = gui.getListBuilder(iconSize);
  auto& sites = c.getSites();
  for (int y : sites.getBounds().getYRange()) {
    auto columns = gui.getListBuilder(iconSize);
    for (int x : sites.getBounds().getXRange()) {
      vector<PGuiElem> v;
      for (int i : All(sites[x][y].viewId)) {
        v.push_back(gui.asciiBackground(sites[x][y].viewId[i]));
        if (i == 0)
          v.push_back(gui.viewObject(sites[x][y].viewId[i], iconScale));
        else {
          if (sites[x][y].viewId[i] == ViewId::CANIF_TREE || sites[x][y].viewId[i] == ViewId::DECID_TREE)
            v.push_back(gui.topMargin(1 * iconScale,
                  gui.viewObject(ViewId::ROUND_SHADOW, iconScale, Color(255, 255, 255, 160))));
          v.push_back(gui.topMargin(-2 * iconScale, gui.viewObject(sites[x][y].viewId[i], iconScale)));
        }
      }
      columns.addElem(gui.stack(std::move(v)));
    }
    auto columns2 = gui.getListBuilder(iconSize);
    for (int x : sites.getBounds().getXRange()) {
      Vec2 pos(x, y);
      vector<PGuiElem> elem;
      if (auto id = sites[x][y].getDwellerViewId()) {
        elem.push_back(gui.asciiBackground(*id));
        if (c.getPlayerPos() && c.isInInfluence(pos))
          elem.push_back(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale,
                sites[pos].isEnemy() ? colors[ColorId::RED] : colors[ColorId::GREEN]));
        if (c.getPlayerPos() == pos && (!marked || !*marked)) // hacky way of checking this is adventurer embark position
          elem.push_back(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale));
        if (activeFun(pos))
          elem.push_back(gui.stack(
                gui.button([pos, clickFun] { clickFun(pos); }),
                gui.mouseHighlight2(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale))));
        elem.push_back(gui.topMargin(1 * iconScale,
              gui.viewObject(ViewId::ROUND_SHADOW, iconScale, Color(255, 255, 255, 160))));
        if (marked)
          elem.push_back(gui.conditional(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale),
                [marked, pos] { return *marked == pos;}));
        elem.push_back(gui.topMargin(-2 * iconScale, gui.viewObject(*id, iconScale)));
        if (c.isDefeated(pos))
          elem.push_back(gui.viewObject(ViewId::TERROR_TRAP, iconScale));
      } else {
        if (activeFun(pos))
          elem.push_back(gui.stack(
                gui.button([pos, clickFun] { clickFun(pos); }),
                gui.mouseHighlight2(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale))));
        if (marked)
          elem.push_back(gui.conditional(gui.viewObject(ViewId::SQUARE_HIGHLIGHT, iconScale),
                [marked, pos] { return *marked == pos;}));
      }
      if (auto desc = sites[x][y].getDwellerDescription())
        elem.push_back(gui.tooltip({*desc}, milliseconds{0}));
      columns2.addElem(gui.stack(std::move(elem)));
    }
    rows.addElem(gui.stack(columns.buildHorizontalList(), columns2.buildHorizontalList()));
  }
  return gui.stack(
    gui.miniBorder2(),
    gui.margins(rows.buildVerticalList(), 8));
}

PGuiElem GuiBuilder::drawWorldmap(Semaphore& sem, const Campaign& campaign) {
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  lines.addElem(gui.centerHoriz(gui.label("Map of " + campaign.getWorldName())));
  lines.addElem(gui.centerHoriz(gui.label("Use the travel command while controlling a minion or team "
          "to travel to another site.", Renderer::smallTextSize, colors[ColorId::LIGHT_GRAY])));
  lines.addElemAuto(gui.centerHoriz(drawCampaignGrid(campaign, nullptr,
      [&campaign](Vec2 pos) { return false; },
      [&campaign](Vec2 pos) { })));
  lines.addBackElem(gui.centerHoriz(
        gui.stack(
          gui.button([&] { sem.v(); }),
          gui.labelHighlight("[Close]", colors[ColorId::LIGHT_BLUE]))
        ));
  return gui.stack(
      gui.preferredSize(1000, 630),
      gui.window(gui.margins(lines.buildVerticalList(), 15), [&sem] { sem.v(); }));
}

PGuiElem GuiBuilder::drawChooseSiteMenu(SyncQueue<optional<Vec2>>& queue, const string& message,
    const Campaign& campaign, optional<Vec2>& sitePos) {
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  lines.addElem(gui.centerHoriz(gui.label(message)));
  lines.addElemAuto(gui.centerHoriz(drawCampaignGrid(campaign, &sitePos,
      [&campaign](Vec2 pos) { return campaign.canTravelTo(pos); },
      [&campaign, &sitePos](Vec2 pos) { sitePos = pos; })));
  lines.addBackElem(gui.centerHoriz(gui.getListBuilder()
        .addElemAuto(gui.conditional(
            gui.stack(
                gui.button([&] { queue.push(*sitePos); }),
                gui.labelHighlight("[Confirm]", colors[ColorId::LIGHT_BLUE])),
            gui.label("[Confirm]", colors[ColorId::LIGHT_GRAY]),
            [&] { return !!sitePos; }))
        .addSpace(10)
        .addElemAuto(
            gui.stack(
                gui.button([&queue] { queue.push(none); }, gui.getKey(SDL::SDLK_ESCAPE), true),
                gui.labelHighlight("[Cancel]", colors[ColorId::LIGHT_BLUE]))).buildHorizontalList()));
  return gui.stack(
      gui.preferredSize(1000, 600),
      gui.window(gui.margins(lines.buildVerticalList(), 15), [&queue] { queue.push(none); }));
}

PGuiElem GuiBuilder::drawPlusMinus(function<void(int)> callback, bool canIncrease, bool canDecrease) {
  return gui.getListBuilder()
      .addElemAuto(canIncrease
          ? gui.stack(
                gui.labelHighlight("[+]", colors[ColorId::LIGHT_BLUE]),
                gui.button([callback] { callback(1); }))
          : gui.label("[+]", colors[ColorId::GRAY]))
      .addSpace(10)
      .addElemAuto(canDecrease
          ? gui.stack(
                gui.labelHighlight("[-]", colors[ColorId::LIGHT_BLUE]),
                gui.button([callback] { callback(-1); }))
          : gui.label("[-]", colors[ColorId::GRAY]))
      .buildHorizontalList();
}

PGuiElem GuiBuilder::drawOptionElem(Options* options, OptionId id, function<void()> onChanged) {
  auto line = gui.getListBuilder();
  string value = options->getValueString(id);
  string name = options->getName(id);
  line.addElem(gui.label(name + ": " + value), 280);
  line.addSpace(30);
  switch (options->getType(id)) {
    case Options::STRING:
      line.addElemAuto(gui.stack(
          gui.button([=] {
              if (auto val = getTextInput("Enter " + name, value, 10, "Leave blank to use a random name.")) {
                options->setValue(id, *val);
                onChanged();
              }}),
          gui.labelHighlight("[Change]", colors[ColorId::LIGHT_BLUE])));
      break;
    case Options::INT: {
      auto limits = options->getLimits(id);
      int value = options->getIntValue(id);
      line.addElemAuto(drawPlusMinus([=] (int v) {
            options->setValue(id, value + v); onChanged();}, value < limits->second, value > limits->first));
      }
      break;
    default:
      break;
  }
  return line.buildHorizontalList();
}

GuiFactory::ListBuilder GuiBuilder::drawRetiredGames(RetiredGames& retired, function<void()> reloadCampaign,
    bool active) {
  auto lines = gui.getListBuilder(legendLineHeight);
  vector<RetiredGames::RetiredGame> allGames = retired.getAllGames();
  for (int i : All(allGames)) {
    if (i == retired.getNumLocal() && !active)
      lines.addElem(gui.label("Online dungeons:", colors[ColorId::YELLOW]));
    if (retired.isActive(i) == active) {
      auto header = gui.getListBuilder();
      if (retired.isActive(i))
        header.addElem(gui.stack(
              gui.labelUnicode(u8"✘", colors[ColorId::RED]),
              gui.button([i, reloadCampaign, &retired] { retired.setActive(i, false); reloadCampaign();})), 15);
      header.addElem(gui.label(allGames[i].gameInfo.getName()), 170);
      for (auto& minion : allGames[i].gameInfo.getMinions())
        header.addElem(drawMinionAndLevel(minion.viewId, minion.level, 1), 25);
      header.addSpace(20);
      if (allGames[i].numTotal > 0 && !active)
        header.addElemAuto(gui.stack(
          gui.tooltip({"Number of times this dungeon has been conquered over how many times it has been loaded."}),
          gui.label("Conquer rate: " + toString(allGames[i].numWon) + "/" + toString(allGames[i].numTotal))));
      PGuiElem line = header.buildHorizontalList();
      if (allGames[i].numTotal > 0 && active)
        line = gui.stack(std::move(line), gui.tooltip({
              "Conquer rate: " + toString(allGames[i].numWon) + "/" + toString(allGames[i].numTotal)}));
      if (!retired.isActive(i))
        line = gui.stack(
            gui.uiHighlightMouseOver(colors[ColorId::GREEN]),
            std::move(line),
            gui.button([i, reloadCampaign, &retired] { retired.setActive(i, true); reloadCampaign();}));
      lines.addElem(std::move(line));
    }
  }
  return lines;
}

PGuiElem GuiBuilder::drawCampaignMenu(SyncQueue<CampaignAction>& queue, const Campaign& campaign, Options* options,
    RetiredGames& retiredGames, optional<Vec2>& embarkPos, bool& retiredMenu, bool& helpText) {
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  int optionMargin = 50;
  lines.addElem(gui.stack(
      gui.leftMargin(optionMargin, gui.label("World name: " + campaign.getWorldName())),
      gui.centerHoriz(gui.stack(
            gui.labelHighlight("[Help]", colors[ColorId::LIGHT_BLUE]),
            gui.button([&] { helpText = true; })))));
  for (OptionId id : campaign.getOptions(options))
    lines.addElem(gui.leftMargin(optionMargin, drawOptionElem(options, id,
            [&queue, id] { queue.push({CampaignActionId::UPDATE_OPTION, id});})));
  lines.addSpace(10);
  lines.addElem(gui.centerHoriz(gui.label(campaign.getSiteChoiceTitle())));
  lines.addElemAuto(gui.centerHoriz(drawCampaignGrid(campaign, &embarkPos,
        [&campaign](Vec2 pos) { return campaign.canEmbark(pos); },
        [&campaign, &queue](Vec2 pos) { queue.push({CampaignActionId::CHOOSE_SITE, pos}); })));
  lines.addBackElem(gui.topMargin(10, gui.centerHoriz(gui.getListBuilder()
        .addElemAuto(gui.conditional(
            gui.stack(
                gui.button([&] { queue.push(CampaignActionId::CONFIRM); }),
                gui.labelHighlight("[Confirm]", colors[ColorId::LIGHT_BLUE])),
            gui.label("[Confirm]", colors[ColorId::LIGHT_GRAY]),
            [&campaign] { return !!campaign.getPlayerPos(); }))
        .addSpace(10)
        .addElemAuto(
          gui.stack(
            gui.button([&queue] { queue.push(CampaignActionId::REROLL_MAP);}),
            gui.labelHighlight("[Re-roll map]", colors[ColorId::LIGHT_BLUE])))
        .addSpace(10)
        .addElemAuto(
            gui.stack(
                gui.button([&queue] { queue.push(CampaignActionId::CANCEL); }, gui.getKey(SDL::SDLK_ESCAPE)),
                gui.labelHighlight("[Cancel]", colors[ColorId::LIGHT_BLUE]))).buildHorizontalList())));
  int retiredPosX = 600;
  int retiredMenuX = 380;
  int helpPosX = 300;
  int menuPosY = 5 * legendLineHeight;
  GuiFactory::ListBuilder retiredList = drawRetiredGames(retiredGames,
      [&queue] { queue.push(CampaignActionId::UPDATE_MAP);}, false);
  if (retiredList.isEmpty())
    retiredList.addElem(gui.label("No retired dungeons found :("));
  PGuiElem interior = gui.stack(makeVec<PGuiElem>(
      lines.buildVerticalList(),
      gui.setHeight(5 * legendLineHeight, gui.alignment(GuiFactory::Alignment::BOTTOM, gui.leftMargin(retiredPosX,
          drawRetiredGames(retiredGames, [&queue] { queue.push(CampaignActionId::UPDATE_MAP);}, true)
              .addElem(gui.conditional(
                  gui.stack(
                      gui.button([&retiredMenu] { retiredMenu = !retiredMenu;}),
                      gui.labelHighlight("[Add retired dungeon]", colors[ColorId::LIGHT_BLUE])),
                  gui.label("[Add retired dungeon]", colors[ColorId::GRAY]),
                  [&retiredGames] { return retiredGames.getNumActive() < 4;}))
              .buildVerticalList()))),
      gui.conditional(gui.topMargin(menuPosY, gui.leftMargin(retiredMenuX,
            gui.setWidth(550, gui.setHeight(min(500, retiredList.getSize() + 30),
              gui.miniWindow2(gui.scrollable(retiredList.buildVerticalList()),
          [&retiredMenu] { retiredMenu = false;}))))),
          [&retiredMenu] { return retiredMenu;}),
      gui.conditional(gui.margins(gui.miniWindow2(gui.margins(
              gui.labelMultiLine(campaign.getIntroText(), legendLineHeight), 10),
          [&helpText] { helpText = false;}), 100, 50, 100, 280),
          [&helpText] { return helpText;})
      ));
  return gui.stack(
      gui.preferredSize(1000, 735),
      gui.window(gui.margins(std::move(interior), 5), [&queue] { queue.push(CampaignActionId::CANCEL); }));
}

PGuiElem GuiBuilder::drawCreaturePrompt(SyncQueue<bool>& queue, const string& title,
    const vector<CreatureInfo>& creatures) {
  auto lines = gui.getListBuilder(getStandardLineHeight() + 10);
  lines.addElem(gui.centerHoriz(gui.label(title)));
  const int windowWidth = 450;
  auto line = gui.getListBuilder(60);
  for (auto& elem : creatures) {
    line.addElem(gui.stack(
          gui.viewObject(elem.viewId, 2),
          gui.label(toString(elem.expLevel), 20)));
    if (line.getSize() >= windowWidth - 50) {
      lines.addElem(gui.centerHoriz(line.buildHorizontalList()), 70);
      line.clear();
    }
  }
  if (!line.isEmpty())
      lines.addElem(gui.centerHoriz(line.buildHorizontalList()), 70);
  lines.addElem(gui.centerHoriz(gui.getListBuilder()
        .addElemAuto(gui.stack(
          gui.labelHighlight("[Ok]", colors[ColorId::LIGHT_BLUE]),
          gui.button([&queue] { queue.push(true);})))
        .addElemAuto(gui.stack(
          gui.labelHighlight("[Cancel]", colors[ColorId::LIGHT_BLUE]),
          gui.button([&queue] { queue.push(false);}))).buildHorizontalList()));
  int margin = 25;
  int height = 2 * margin + lines.getSize() + 30;
  return gui.stack(
      gui.preferredSize(2 * margin + windowWidth, height),
      gui.window(gui.margins(lines.buildVerticalList(), margin), [&queue] { queue.push(false); }));

}

PGuiElem GuiBuilder::drawTeamLeaderMenu(SyncQueue<optional<UniqueEntity<Creature>::Id>>& queue, const string& title,
      const vector<CreatureInfo>& team, const string& cancelText) {
  auto lines = gui.getListBuilder(getStandardLineHeight() + 10);
  lines.addElem(gui.centerHoriz(gui.label(title)));
  const int windowWidth = 450;
  auto line = gui.getListBuilder(60);
  for (auto& elem : team) {
    line.addElem(gui.stack(
          gui.mouseHighlight2(gui.bottomMargin(22, gui.rightMargin(6,
                gui.icon(gui.HIGHLIGHT, GuiFactory::Alignment::CENTER_STRETCHED, colors[ColorId::GREEN])))),
          gui.button([&queue, elem] { queue.push(elem.uniqueId); }),
          gui.viewObject(elem.viewId, 2),
          gui.label(toString(elem.expLevel), 20)));
    if (line.getSize() >= windowWidth - 50) {
      lines.addElem(gui.centerHoriz(line.buildHorizontalList()), 70);
      line.clear();
    }
  }
  if (!line.isEmpty())
      lines.addElem(gui.centerHoriz(line.buildHorizontalList()), 70);
  if (!cancelText.empty())
    lines.addElem(gui.centerHoriz(gui.stack(
          gui.labelHighlight("[" + cancelText + "]", colors[ColorId::LIGHT_BLUE]),
          gui.button([&queue] { queue.push(none);}))));
  int margin = 25;
  int height = 2 * margin + lines.getSize() + 30;
  return gui.stack(
      gui.preferredSize(2 * margin + windowWidth, height),
      gui.window(gui.margins(lines.buildVerticalList(), margin), [&queue] { queue.push(none); }));
}

PGuiElem GuiBuilder::drawRecruitMenu(SyncQueue<optional<UniqueEntity<Creature>::Id>>& queue, const string& title,
    const string& warning, pair<ViewId, int> budget, const vector<CreatureInfo>& creatures, double* scrollPos) {
  GuiFactory::ListBuilder lines(gui, getStandardLineHeight());
  lines.addElem(GuiFactory::ListBuilder(gui)
      .addElemAuto(gui.label(title))
      .addBackElemAuto(drawCost(budget)).buildHorizontalList());
  if (!warning.empty()) {
    lines.addElem(gui.label(warning, colors[ColorId::RED]));
    budget.second = -1;
  }
  lines.addSpace(10);
  for (PGuiElem& elem : drawRecruitList(creatures,
        [&queue] (optional<UniqueEntity<Creature>::Id> a) { queue.push(a);}, budget.second))
    lines.addElem(std::move(elem));
  int menuHeight = lines.getSize() + 30;
  return gui.stack(
      gui.preferredSize(330, menuHeight),
      gui.miniWindow(gui.margins(gui.scrollable(lines.buildVerticalList(), scrollPos), 15),
          [&queue] { queue.push(none); }));
}

vector<PGuiElem> GuiBuilder::drawRecruitList(const vector<CreatureInfo>& creatures,
    CreatureMenuCallback callback, int budget) {
  vector<CreatureMapElem> stacks = getRecruitStacks(creatures);
  vector<PGuiElem> lines;
  for (auto& elem : stacks) {
    bool canAfford = elem.any.cost->second <= budget;
    ColorId color = canAfford ? ColorId::WHITE : ColorId::GRAY;
    lines.push_back(gui.stack(
        gui.keyHandler([callback] { callback(none); }, {gui.getKey(SDL::SDLK_ESCAPE), gui.getKey(SDL::SDLK_RETURN)}),
        canAfford ? gui.button([callback, elem] { callback(elem.any.uniqueId); }) : gui.empty(),
        gui.leftMargin(25, gui.stack(
            canAfford ? gui.mouseHighlight2(gui.highlight(listLineHeight)) : gui.empty(),
            GuiFactory::ListBuilder(gui)
                  .addElemAuto(gui.rightMargin(10, gui.label(toString(elem.count), colors[color])))
                  .addElem(gui.viewObject(elem.viewId), 50)
                  .addElem(gui.label("level " + toString(elem.any.expLevel), colors[color]), 50)
                  .addBackElemAuto(drawCost(*elem.any.cost, color)).buildHorizontalList()))));
  }
  return lines;
}

PGuiElem GuiBuilder::drawHighscorePage(const HighscoreList& page, double *scrollPos) {
  GuiFactory::ListBuilder lines(gui, legendLineHeight);
  for (auto& elem : page.scores) {
    GuiFactory::ListBuilder line(gui);
    ColorId color = elem.highlight ? ColorId::GREEN : ColorId::WHITE;
    line.addElemAuto(gui.label(elem.text, colors[color]));
    if (page.sortBy == page.SCORE)
      line.addBackElem(gui.label(toString(elem.score) + " points", colors[color]), 130);
    else
      line.addBackElem(gui.label(toString(elem.turns) + " turns", colors[color]), 130);
    lines.addElem(gui.leftMargin(30, line.buildHorizontalList()));
  }
  return gui.scrollable(lines.buildVerticalList(), scrollPos);
}

PGuiElem GuiBuilder::drawHighscores(const vector<HighscoreList>& list, Semaphore& sem, int& tabNum,
    vector<double>& scrollPos, bool& online) {
  vector<PGuiElem> pages;
  int numTabs = list.size() / 2;
  GuiFactory::ListBuilder topLine(gui, 200);
  for (int i : All(list)) {
    for (int j : All(list[i].scores))
      if (list[i].scores[j].highlight) {
        if (i < numTabs)
          tabNum = i;
        scrollPos[i] = j;
      }
    pages.push_back(gui.conditional(drawHighscorePage(list[i], &scrollPos[i]),
          [&tabNum, i, &online, numTabs] { return (online && tabNum == i - numTabs) ||
              (!online && tabNum == i) ; }));
    if (i < numTabs)
    topLine.addElem(gui.stack(
        gui.margins(gui.uiHighlightConditional([&tabNum, i] { return tabNum == i;}), 42, 0, 32, 0),
        gui.centeredLabel(Renderer::HOR, list[i].name),
        gui.button([&tabNum, i] { tabNum = i;})));
  }
  PGuiElem onlineBut = gui.stack(
      gui.label("Online", [&online] { return colors[online ? ColorId::GREEN : ColorId::WHITE];}),
      gui.button([&online] { online = !online; }));
  Vec2 size = getMenuPosition(MenuType::NORMAL, 0).getSize();
  return gui.stack(makeVec<PGuiElem>(gui.preferredSize(size.x, size.y),
      gui.keyHandler([&tabNum, numTabs] { tabNum = (tabNum + 1) % numTabs; }, {gui.getKey(SDL::SDLK_RIGHT)}),
      gui.keyHandler([&tabNum, numTabs] { tabNum = (tabNum + numTabs - 1) % numTabs; }, {gui.getKey(SDL::SDLK_LEFT)}),
      gui.window(
        gui.margin(gui.leftMargin(25, std::move(onlineBut)),
        gui.topMargin(30, gui.margin(gui.leftMargin(5, topLine.buildHorizontalListFit()),
            gui.margins(gui.stack(std::move(pages)), 25, 60, 0, 30), legendLineHeight, GuiFactory::TOP)), legendLineHeight, GuiFactory::TOP),
        [&] { sem.v(); })));

}

Rectangle GuiBuilder::getTextInputPosition() {
  Vec2 center = renderer.getSize() / 2;
  return Rectangle(center - Vec2(300, 129), center + Vec2(300, 129));
}

PGuiElem GuiBuilder::getTextContent(const string& title, const string& value, const string& hint) {
  auto lines = gui.getListBuilder(legendLineHeight);
  lines.addElem(
      gui.variableLabel([&] { return title + ":  " + value + "_"; }, legendLineHeight), 3 * legendLineHeight);
  if (!hint.empty())
    lines.addElem(gui.label(hint, gui.inactiveText));
  return lines.buildVerticalList();
}

optional<string> GuiBuilder::getTextInput(const string& title, const string& value, int maxLength,
    const string& hint) {
  bool dismiss = false;
  string text = value;
  PGuiElem dismissBut = gui.margins(gui.stack(makeVec<PGuiElem>(
        gui.button([&](){ dismiss = true; }),
        gui.mouseHighlight2(gui.mainMenuHighlight()),
        gui.centerHoriz(
            gui.label("Dismiss", colors[ColorId::WHITE]), renderer.getTextLength("Dismiss")))), 0, 5, 0, 0);
  PGuiElem stuff = gui.margins(getTextContent(title, text, hint), 30, 50, 0, 0);
  stuff = gui.margin(gui.centerHoriz(std::move(dismissBut), renderer.getTextLength("Dismiss") + 100),
    std::move(stuff), 30, gui.BOTTOM);
  stuff = gui.window(std::move(stuff), [&dismiss] { dismiss = true; });
  PGuiElem bg = gui.darken();
  bg->setBounds(renderer.getSize());
  SDL::SDL_StartTextInput();
  OnExit tmp([]{ SDL::SDL_StopTextInput();});
  while (1) {
    callbacks.refreshScreen();
    bg->render(renderer);
    stuff->setBounds(getTextInputPosition());
    stuff->render(renderer);
    renderer.drawAndClearBuffer();
    Event event;
    while (renderer.pollEvent(event)) {
      gui.propagateEvent(event, {stuff.get()});
      if (dismiss)
        return none;
      if (event.type == SDL::SDL_TEXTINPUT)
        if (text.size() < maxLength)
          text += event.text.text;
/*        if ((isalnum(event.text.unicode) || event.text.unicode == ' ') && text.size() < maxLength)
          text += event.text.unicode;*/
      if (event.type == SDL::SDL_KEYDOWN)
        switch (event.key.keysym.sym) {
          case SDL::SDLK_BACKSPACE:
              if (!text.empty())
                text.pop_back();
              break;
          case SDL::SDLK_KP_ENTER:
          case SDL::SDLK_RETURN: return text;
          case SDL::SDLK_ESCAPE: return none;
          default: break;
        }
    }
  }
}
