#include <Swoosh/ActivityController.h>
#include <Segues/PushIn.h>
#include <Segues/Checkerboard.h>
#include <Segues/PixelateBlackWashFade.h>
#include <Segues/DiamondTileSwipe.h>

#include "bnOverworldSceneBase.h"

#include "../bnWebClientMananger.h"
#include "../Android/bnTouchArea.h"

#include "../bnFolderScene.h"
#include "../bnSelectNaviScene.h"
#include "../bnSelectMobScene.h"
#include "../bnLibraryScene.h"
#include "../bnConfigScene.h"
#include "../bnFolderScene.h"

#include "../bnCardFolderCollection.h"
#include "../bnLanBackground.h"
#include "../netplay/bnPVPScene.h"

using sf::RenderWindow;
using sf::VideoMode;
using sf::Clock;
using sf::Event;
using sf::Font;
using namespace swoosh::types;


/// \brief Thunk to populate menu options to callbacks
auto MakeOptions = [] (Overworld::SceneBase* scene) -> MenuWidget::OptionsList {
  return {
    { "chip_folder", std::bind(&Overworld::SceneBase::GotoChipFolder, scene) },
    { "navi",        std::bind(&Overworld::SceneBase::GotoNaviSelect, scene) },
    { "mob_select",  std::bind(&Overworld::SceneBase::GotoMobSelect, scene) },
    { "config",      std::bind(&Overworld::SceneBase::GotoConfig, scene) },
    { "sync",        std::bind(&Overworld::SceneBase::GotoPVP, scene) }
  };
};

Overworld::SceneBase::SceneBase(swoosh::ActivityController& controller, bool guestAccount) :
  guestAccount(guestAccount),
  camera(ENGINE.GetView()),
  lastIsConnectedState(false),
  menuWidget("Overworld", MakeOptions(this)),
  textbox({ 4, 255 }),
  swoosh::Activity(&controller)
{
  // When we reach the menu scene we need to load the player information
  // before proceeding to next sub menus
  // data = CardFolderCollection::ReadFromFile("resources/database/folders.txt");

  webAccountIcon.setTexture(LOAD_TEXTURE(WEBACCOUNT_STATUS));
  webAccountIcon.setScale(2.f, 2.f);
  webAccountIcon.setPosition(4, getController().getVirtualWindowSize().y - 44.0f);
  webAccountAnimator = Animation("resources/ui/webaccount_icon.animation");
  webAccountAnimator.Load();
  webAccountAnimator.SetAnimation("NO_CONNECTION");

  // Draws the scrolling background
  SetBackground(new LanBackground);

  // Selection input delays
  maxSelectInputCooldown = 0.5; // half of a second
  selectInputCooldown = maxSelectInputCooldown;

  // Keep track of selected navi
  currentNavi = 0;

  menuWidget.setScale(2.f, 2.f);

  gotoNextScene = true;

  /// WEB ACCOUNT LOADING

  WebAccounts::AccountState account;

  if (WEBCLIENT.IsLoggedIn()) {
    bool loaded = WEBCLIENT.LoadSession("profile.bin", &account);

    // Quickly load the session on disk to reduce wait times
    if (loaded) {
      Logger::Log("Found cached account data");

      WEBCLIENT.UseCachedAccount(account);
      WEBCLIENT.CacheTextureData(account);
      folders = CardFolderCollection::ReadFromWebAccount(account);
      programAdvance = PA::ReadFromWebAccount(account);

      NaviEquipSelectedFolder();
    }

    Logger::Log("Fetching account data...");

    // resent fetch command to get the a latest account info
    accountCommandResponse = WEBCLIENT.SendFetchAccountCommand();

    Logger::Log("waiting for server...");
  }
  else {

    // If we are not actively online but we have a profile on disk, try to load our previous session
    // The user may be logging in but has not completed yet and we want to reduce wait times...
    // Otherwise, use the guest profile
    bool loaded = WEBCLIENT.LoadSession("profile.bin", &account) || WEBCLIENT.LoadSession("guest.bin", &account);

    if (loaded) {
      WEBCLIENT.UseCachedAccount(account);
      WEBCLIENT.CacheTextureData(account);
      folders = CardFolderCollection::ReadFromWebAccount(account);
      programAdvance = PA::ReadFromWebAccount(account);

      NaviEquipSelectedFolder();
    }
  }

  setView(sf::Vector2u(480, 320));

  // Spawn overworld player
  playerActor.setPosition(200, 20);
  playerActor.SetCollisionRadius(6);
  playerActor.CollideWithMap(map);
  playerActor.CollideWithQuadTree(quadTree);

  // Share the camera
  map.SetCamera(&camera);
  map.AddSprite(&playerActor, 0);
  map.AddSprite(&teleportController.GetBeam(), 0);
  map.setScale(2.f, 2.f);

  // Load overworld assets
  treeTexture = TEXTURES.LoadTextureFromFile("resources/ow/tree.png");
  treeAnim = Animation("resources/ow/tree.animation") << "default" << Animator::Mode::Loop;

  warpTexture = TEXTURES.LoadTextureFromFile("resources/ow/warp.png");
  warpAnim = Animation("resources/ow/warp.animation") << "default" << Animator::Mode::Loop;

  netWarpTexture = TEXTURES.LoadTextureFromFile("resources/ow/hp_warp.png");
  netWarpAnim = Animation("resources/ow/hp_warp.animation") << "off" << Animator::Mode::Loop;

  homeWarpTexture = TEXTURES.LoadTextureFromFile("resources/ow/home_warp.png");
  homeWarpAnim = Animation("resources/ow/home_warp.animation") << "default" << Animator::Mode::Loop;

  gateTexture = TEXTURES.LoadTextureFromFile("resources/ow/gate.png");
  gateAnim = Animation("resources/ow/gate.animation") << "default" << Animator::Mode::Loop;

  coffeeTexture = TEXTURES.LoadTextureFromFile("resources/ow/coffee.png");
  coffeeAnim = Animation("resources/ow/coffee.animation") << "default" << Animator::Mode::Loop;

  ornamentTexture = TEXTURES.LoadTextureFromFile("resources/ow/xmas.png");
  starAnim = xmasAnim = lightsAnim = Animation("resources/ow/xmas.animation");
  starAnim << "star" << Animator::Mode::Loop;
  lightsAnim << "lights" << Animator::Mode::Loop;

  // reserve 1000 elements so no references are invalidated
  npcs.reserve(1000);
  pathControllers.reserve(1000);
}

Overworld::SceneBase::~SceneBase()
{
  ClearMap(map.GetRows(), map.GetCols());
}

void Overworld::SceneBase::onStart() {
#ifdef __ANDROID__
  StartupTouchControls();
#endif

  // Set the camera back to ours
  ENGINE.SetCamera(camera);

  gotoNextScene = false;
}

void Overworld::SceneBase::onUpdate(double elapsed) {
  if (firstTimeLoad) {
    // Effectively loads the map
    ResetMap();

    firstTimeLoad = false;
  }

  if (menuWidget.IsClosed() && textbox.IsClosed()) {
    playerController.ListenToInputEvents(true);
  }
  else {
    playerController.ListenToInputEvents(false);
  }

  // check to see if talk button was pressed
  if (textbox.IsClosed() && menuWidget.IsClosed()) {
    if (INPUTx.Has(EventTypes::PRESSED_CONFIRM)) {
      // check to see what tile we pressed talk to
      const Overworld::Map::Tile tile = map.GetTileAt(playerActor.PositionInFrontOf());
      if (tile.token == "C") {
        textbox.EnqueMessage({}, "", new Message("A cafe sign.\nYou feel welcomed."));
        textbox.Open();
      }
      else if (tile.token == "G") {
        textbox.EnqueMessage({}, "", new Message("The gate needs a key to get through."));
        textbox.Open();
      }
      else if (tile.token == "X") {
        textbox.EnqueMessage({}, "", new Message("An xmas tree program developed by Mars"));
        textbox.EnqueMessage({}, "", new Message("It really puts you in the holiday spirit!"));
        textbox.Open();
      }
    }
  }
  else if (INPUTx.Has(EventTypes::PRESSED_CONFIRM)) {
    // continue the conversation if the text is complete
    if (textbox.IsEndOfMessage()) {
      textbox.DequeMessage();

      if (!textbox.HasMessage()) {
        // if there are no more messages, close
        textbox.Close();
      }
    }
    else if (textbox.IsEndOfBlock()) {
      textbox.ShowNextLines();
    }
    else {
      // double tapping talk will complete the block
      textbox.CompleteCurrentBlock();
    }
  }

  // handle custom tile collision
  if (!HasTeleportedAway() && teleportController.IsComplete()) {
    auto playerTile = map.GetTileAt(playerActor.getPosition());
    this->OnTileCollision(playerTile);
  }

  /**
  * update all overworld objects and animations
  */

  // objects
  for (auto& pathController : pathControllers) {
    pathController.Update(elapsed);
  }

  if (gotoNextScene == false) {
    playerController.Update(elapsed);
    teleportController.Update(elapsed);
  }

  playerActor.Update(elapsed);
  
  for (auto& npc : npcs) {
    npc.Update(elapsed);
  }

  // animations
  animElapsed += elapsed;

  treeAnim.SyncTime((float)animElapsed);
  for (auto& t : trees) {
    treeAnim.Refresh(t->getSprite());
  };

  static float ribbon_factor = 0;
  static bool ribbon_bounce = false;

  if (ribbon_factor >= 1.25f) {
    ribbon_bounce = true;
  }
  else if (ribbon_factor <= -0.25f) {
    ribbon_bounce = false;
  }

  if (ribbon_bounce) {
    ribbon_factor -= static_cast<float>(elapsed * 1.6);
  }
  else {
    ribbon_factor += static_cast<float>(elapsed * 1.6);
  }

  float input_factor = std::min(ribbon_factor, 1.0f);
  input_factor = std::max(input_factor, 0.0f);

  for (auto& r : ribbons) {
    r->GetShader().SetUniform("factor", input_factor);
  }

  coffeeAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& c : coffees) {
    coffeeAnim.Refresh(c->getSprite());
  }

  gateAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& g : gates) {
    gateAnim.Refresh(g->getSprite());
  }

  warpAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& w : warps) {
    warpAnim.Refresh(w->getSprite());
  }

  netWarpAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& w : netWarps) {
    netWarpAnim.Refresh(w->getSprite());
  }

  homeWarpAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& h : homeWarps) {
    homeWarpAnim.Refresh(h->getSprite());
  }

  starAnim.SyncTime(static_cast<float>(animElapsed));
  for (auto& s : stars) {
    starAnim.Refresh(s->getSprite());
  }

  lightsAnim.SyncTime(static_cast<float>(animElapsed));

  static std::array<sf::Color,5> rgb = {
    sf::Color::Cyan,
    sf::Color{255, 182, 192}, // pinkish red
    sf::Color::Yellow,
    sf::Color::White,
    sf::Color::Magenta
  };

  size_t inc = static_cast<size_t>(animElapsed/0.8); // in seconds to steps
  size_t offset = 0;
  for (auto& L : lights) {
    lightsAnim.Refresh(L->getSprite());
    L->setColor(rgb[(offset+++inc)%rgb.size()]);
  }
  offset = 0;

  #ifdef __ANDROID__
  if(gotoNextScene)
    return; // keep the screen looking the same when we come back
  #endif

  if (WEBCLIENT.IsLoggedIn() && accountCommandResponse.valid() && is_ready(accountCommandResponse)) {
    try {
      const WebAccounts::AccountState& account = accountCommandResponse.get();
      Logger::Logf("You have %i folders on your account", account.folders.size());
      WEBCLIENT.CacheTextureData(account);
      folders = CardFolderCollection::ReadFromWebAccount(account);
      programAdvance = PA::ReadFromWebAccount(account);

      NaviEquipSelectedFolder();

      // Replace
      WEBCLIENT.SaveSession("profile.bin");
    }
    catch (const std::runtime_error& e) {
      Logger::Logf("Could not fetch account.\nError: %s", e.what());
    }
  }

  // update the web connectivity icon
  bool currentConnectivity = WEBCLIENT.IsConnectedToWebServer();
  if (currentConnectivity != lastIsConnectedState) {
    if (WEBCLIENT.IsConnectedToWebServer()) {
       webAccountAnimator.SetAnimation("OK_CONNECTION");
    }
    else {
       webAccountAnimator.SetAnimation("NO_CONNECTION");
    }

    lastIsConnectedState = currentConnectivity;
  }

  // Update the map
  map.Update((float)elapsed);

  // Update the camera
  camera.Update((float)elapsed);
  
  // Loop the bg
  bg->Update((float)elapsed);

  // Update the widget
  menuWidget.Update((float)elapsed);

  // Update the textbox
  textbox.Update(elapsed);

  // Follow the navi
  sf::Vector2f pos = map.ScreenToWorld(playerActor.getPosition());
  pos = sf::Vector2f(pos.x*map.getScale().x, pos.y*map.getScale().y);
  camera.PlaceCamera(pos);

  if (!gotoNextScene && textbox.IsClosed()) {
    if (INPUTx.Has(EventTypes::PRESSED_PAUSE) && !INPUTx.Has(EventTypes::PRESSED_CANCEL)) {
      if (menuWidget.IsClosed()) {
        menuWidget.Open();
        AUDIO.Play(AudioType::CHIP_DESC);
      }
      else if (menuWidget.IsOpen()) {
        menuWidget.Close();
        AUDIO.Play(AudioType::CHIP_DESC_CLOSE);
      }
    }

    if (menuWidget.IsOpen()) {
      if (INPUTx.Has(EventTypes::PRESSED_UI_UP) || INPUTx.Has(EventTypes::HELD_UI_UP)) {
        selectInputCooldown -= elapsed;

        if (selectInputCooldown <= 0) {
          if(!extendedHold) {
            selectInputCooldown = maxSelectInputCooldown;
            extendedHold = true;
          }
          else {
            selectInputCooldown = maxSelectInputCooldown / 4.0;
          }

          menuWidget.CursorMoveUp() ? AUDIO.Play(AudioType::CHIP_SELECT) : 0;
        }
      }
      else if (INPUTx.Has(EventTypes::PRESSED_UI_DOWN) || INPUTx.Has(EventTypes::HELD_UI_DOWN)) {
        selectInputCooldown -= elapsed;

        if (selectInputCooldown <= 0) {
          if (!extendedHold) {
            selectInputCooldown = maxSelectInputCooldown;
            extendedHold = true;
          }
          else {
            selectInputCooldown = maxSelectInputCooldown / 4.0;
          }

          menuWidget.CursorMoveDown() ? AUDIO.Play(AudioType::CHIP_SELECT) : 0;
        }
      }
      else if (INPUTx.Has(EventTypes::PRESSED_CONFIRM)) {
        bool result = menuWidget.ExecuteSelection();

        if (result && menuWidget.IsOpen() == false) {
          AUDIO.Play(AudioType::CHIP_DESC_CLOSE);
        }
      }
      else if (INPUTx.Has(EventTypes::PRESSED_UI_RIGHT) || INPUTx.Has(EventTypes::PRESSED_CANCEL)) {
        extendedHold = false;
        
        bool selectedExit = menuWidget.SelectExit();
        
        if (!selectedExit) {
          // already selected, switch to options
          menuWidget.SelectOptions();
        }

        AUDIO.Play(AudioType::CHIP_SELECT);
      }
      else if (INPUTx.Has(EventTypes::PRESSED_UI_LEFT)) {
        menuWidget.SelectOptions() ? AUDIO.Play(AudioType::CHIP_SELECT) : 0;
        extendedHold = false;
      }
      else {
        extendedHold = false;
        selectInputCooldown = 0;
      }
    }
  }

  // Allow player to resync with remote account by pressing the pause action
  if (INPUTx.Has(EventTypes::PRESSED_QUICK_OPT)) {
      accountCommandResponse = WEBCLIENT.SendFetchAccountCommand();
  }

  webAccountAnimator.Update((float)elapsed, webAccountIcon.getSprite());
}

void Overworld::SceneBase::onLeave() {
    #ifdef __ANDROID__
    ShutdownTouchControls();
    #endif
}

void Overworld::SceneBase::onExit()
{

}

void Overworld::SceneBase::onEnter()
{
  RefreshNaviSprite();
}

void Overworld::SceneBase::onResume() {

  guestAccount = !WEBCLIENT.IsLoggedIn();

  if (!guestAccount) {
    accountCommandResponse = WEBCLIENT.SendFetchAccountCommand();
  }
  else {
    try {
      WEBCLIENT.SaveSession("guest.bin");
    }
    catch (const std::runtime_error& e) {
      Logger::Logf("Could not fetch account.\nError: %s", e.what());
    }
  }

  gotoNextScene = false;

  ENGINE.SetCamera(camera);

  // if we left this scene for a new OW scene... return to our warp area
  if (teleportedOut) {
    playerController.ReleaseActor();

    auto& command = teleportController.TeleportIn(
      playerActor, 
      returnPoint, 
      Reverse(playerActor.GetHeading())
    );

    command.onFinish.Slot([=] {
      teleportedOut = false;
      playerController.ControlActor(playerActor);
    });
  }

#ifdef __ANDROID__
  StartupTouchControls();
#endif
}

void Overworld::SceneBase::onDraw(sf::RenderTexture& surface) {
  ENGINE.SetRenderSurface(surface);

  ENGINE.Draw(bg);

  auto offset = ENGINE.GetViewOffset();
  map.move(offset);
  ENGINE.Draw(map);
  map.move(-offset);

  ENGINE.Draw(menuWidget);

  // Add the web account connection symbol
  ENGINE.Draw(&webAccountIcon);

  ENGINE.Draw(textbox);
}

void Overworld::SceneBase::onEnd() {
  ENGINE.RevokeShader();

#ifdef __ANDROID__
  ShutdownTouchControls();
#endif
}

void Overworld::SceneBase::RefreshNaviSprite()
{
  // Only refresh all data and graphics if this is a new navi
  if (lastSelectedNavi == currentNavi) return;

  lastSelectedNavi = currentNavi;

  auto& meta = NAVIS.At(currentNavi);

  // refresh menu widget too
  int hp = std::atoi(meta.GetHPString().c_str());
  menuWidget.SetHealth(hp);
  menuWidget.SetMaxHealth(hp);

  // If coming back from navi select, the navi has changed, update it
  auto owPath = meta.GetOverworldAnimationPath();

  if (owPath.size()) {
    playerActor.setTexture(meta.GetOverworldTexture());
    playerActor.LoadAnimations(owPath);

    auto iconTexture = meta.GetIconTexture();

    if (iconTexture) {
      menuWidget.UseIconTexture(iconTexture);
    }
    else {
      menuWidget.ResetIconTexture();
    }
  }
  else {
    Logger::Logf("Overworld animation not found for navi at index %i", currentNavi);
  }
}

void Overworld::SceneBase::NaviEquipSelectedFolder()
{
  auto naviStr = WEBCLIENT.GetValue("SelectedNavi");
  if (!naviStr.empty()) {
    currentNavi = SelectedNavi(atoi(naviStr.c_str()));
    RefreshNaviSprite();

    auto folderStr = WEBCLIENT.GetValue("FolderFor:" + naviStr);
    if (!folderStr.empty()) {
      // preserve our selected folder
      if (int index = folders.FindFolder(folderStr); index >= 0) {
        folders.SwapOrder(index, 0); // Select this folder again
      }
    }
  }
  else {
    currentNavi = SelectedNavi(0);
    WEBCLIENT.SetKey("SelectedNavi", std::to_string(0));
  }
}

void Overworld::SceneBase::ClearMap(unsigned rows, unsigned cols)
{
  // cleanup the previous tiles
  for (unsigned i = 0; i < rows; i++) {
    delete[] tiles[i];
  }

  if (rows > 0 && cols > 0) {
    delete[] tiles;
  }

  // clean up sprites
  std::vector<std::shared_ptr<SpriteProxyNode>> nodes;
  nodes.insert(nodes.end(), trees.begin(), trees.end());
  nodes.insert(nodes.end(), warps.begin(), warps.end());
  nodes.insert(nodes.end(), gates.begin(), gates.end());
  nodes.insert(nodes.end(), coffees.begin(), coffees.end());

  for (auto& node : nodes) {
    map.RemoveSprite(&*node);
  }

  // clear the actor lists
  for (auto& npc : npcs) {
    map.RemoveSprite(&npc);
  }

  npcs.clear();
  pathControllers.clear();
  quadTree.actors.clear();

  // delete allocated attachments
  for (auto& node : stars) {
    delete node;
  }

  for (auto& node : bulbs) {
    delete node;
  }

  for (auto& node : lights) {
    delete node;
  }

  trees.clear();
  lights.clear();
  warps.clear();
  gates.clear();
  coffees.clear();
  bulbs.clear();
  stars.clear();
  ribbons.clear();
}

void Overworld::SceneBase::ResetMap()
{
  // Load a map
  unsigned lastMapRows = map.GetRows();
  unsigned lastMapCols = map.GetCols();

  auto result = FetchMapData();

  if (!result.first) {
    Logger::Log("Failed to load map homepage.txt");
  }
  else {
    ClearMap(lastMapRows, lastMapCols);

    // Pre-populate the quadtree with the player
    quadTree.actors.push_back(&playerActor);

    // Place some objects in the scene
    auto iceman_pos = map.FindToken("I");;
    for (auto pos : iceman_pos) {
      npcs.emplace_back(Overworld::Actor{ "Iceman" });
      auto* iceman = &npcs.back();

      pathControllers.emplace_back(Overworld::PathController{});
      auto* pathController = &pathControllers.back();

      // Test iceman
      iceman->LoadAnimations("resources/mobs/iceman/iceman_OW.animation");
      iceman->setTexture(TEXTURES.LoadTextureFromFile("resources/mobs/iceman/iceman_OW.png"));
      iceman->setPosition(pos);
      iceman->SetCollisionRadius(6);
      iceman->CollideWithMap(map);
      iceman->CollideWithQuadTree(quadTree);
      iceman->SetInteractCallback([=](Overworld::Actor& with) {
        // Interrupt pathing until new condition is met
        pathController->InterruptUntil([=] {
          return textbox.IsClosed();
          });
        // if player interacted with us
        if (&with == &playerActor && textbox.IsClosed()) {
          // Face them
          iceman->Face(Reverse(with.GetHeading()));

          // Play message
          sf::Sprite face;
          face.setTexture(*TEXTURES.LoadTextureFromFile("resources/mobs/iceman/ice_mug.png"));
          textbox.EnqueMessage(face, "resources/mobs/iceman/mug.animation", new Message("Can't talk! Xmas is only 30 days away!"));
          textbox.Open();
        }
        });

      pathController->ControlActor(*iceman);
      map.AddSprite(iceman,0);
      quadTree.actors.push_back(iceman);
    }

    // Place some objects in the scene
    auto mrprog_pos = map.FindToken("P");
    for (auto pos : mrprog_pos) {
      npcs.emplace_back(Overworld::Actor{ "Mr. Prog" });
      auto* mrprog = &npcs.back();

      pathControllers.emplace_back(Overworld::PathController{});
      auto* pathController = &pathControllers.back();

      // Test Mr Prog
      mrprog->LoadAnimations("resources/ow/prog/prog_ow.animation");
      mrprog->setTexture(TEXTURES.LoadTextureFromFile("resources/ow/prog/prog_ow.png"));
      mrprog->setPosition(pos);
      mrprog->SetCollisionRadius(3);
      mrprog->CollideWithMap(map);
      mrprog->CollideWithQuadTree(quadTree);
      mrprog->SetInteractCallback([=](Overworld::Actor& with) {
        // if player interacted with us
        if (&with == &playerActor && textbox.IsClosed()) {
          // Face them
          mrprog->Face(Reverse(with.GetHeading()));

          // Play message
          sf::Sprite face;
          face.setTexture(*TEXTURES.LoadTextureFromFile("resources/ow/prog/prog_mug.png"));
          textbox.EnqueMessage(face, "resources/ow/prog/prog_mug.animation",
            new Message("This is your homepage! Find an active telepad to take you into cyberspace!")
          );
          textbox.Open();
        }
      });

      pathController->ControlActor(*mrprog);
      map.AddSprite(mrprog, 0);
      quadTree.actors.push_back(mrprog);
    }

    auto trees = map.FindToken("T");
    auto treeWBulb = map.FindToken("L");
    auto xmasTree = map.FindToken("X");
    auto warps = map.FindToken("W");
    auto coff = map.FindToken("C");
    auto gates = map.FindToken("G");
    auto paths = map.FindToken("#");
    auto netWarps = map.FindToken("@");
    auto homeWarps = map.FindToken("H");
   
    auto cyberworldWarp = map.FindToken("N");
    warps.insert(warps.end(), cyberworldWarp.begin(), cyberworldWarp.end());

    playerController.ReleaseActor();

    // Determine where to spawn the player...
    bool firstWarp = true;

    // coffee
    for (auto pos : coff) {
      auto new_coffee = std::make_shared<SpriteProxyNode>(*coffeeTexture);
      new_coffee->setPosition(pos);
      this->coffees.push_back(new_coffee);
      map.AddSprite(&*new_coffee, 0);

      // do not walk through coffee displays
      auto tile = map.GetTileAt(pos);
      tile.solid = true;
      tile.ID = 2;
      map.SetTileAt(pos, tile);
    }

    // gates
    for (auto pos : gates) {
      auto new_gate = std::make_shared<SpriteProxyNode>(*gateTexture);
      new_gate->setPosition(pos);
      this->gates.push_back(new_gate);
      map.AddSprite(&*new_gate, 0);

      // do not walk through gates
      auto tile = map.GetTileAt(pos);
      tile.solid = true;
      tile.ID = 3; // make red
      map.SetTileAt(pos, tile);
    }

    // warps
    for (auto pos : warps) {
      auto new_warp = std::make_shared<SpriteProxyNode>(*warpTexture);
      new_warp->setPosition(pos);
      this->warps.push_back(new_warp);
      map.AddSprite(&*new_warp, 1);
    }

    // warp from net back to homepage
    for (auto pos : homeWarps) {
      auto new_warp = std::make_shared<SpriteProxyNode>(*homeWarpTexture);
      new_warp->setPosition(pos);
      this->homeWarps.push_back(new_warp);
      map.AddSprite(&*new_warp, 1);

      if (firstWarp) {
        firstWarp = false;

        auto& command = teleportController.TeleportIn(playerActor, pos, Direction::up);
        command.onFinish.Slot([=] {
          playerController.ControlActor(playerActor);
          });
      }
    }

    // warp from the computer to the net
    for (auto pos : netWarps) {
      auto new_warp = std::make_shared<SpriteProxyNode>(*netWarpTexture);
      new_warp->setPosition(pos);
      this->netWarps.push_back(new_warp);
      map.AddSprite(&*new_warp, 1);

      if (firstWarp) {
        firstWarp = false;

        auto& command = teleportController.TeleportIn(playerActor, pos, Direction::up);
        command.onFinish.Slot([=] {
          playerController.ControlActor(playerActor);
        });
      }
    }

    // trees
    for (auto pos : trees) {
      auto new_tree = std::make_shared<SpriteProxyNode>(*treeTexture);
      new_tree->setPosition(pos);

      SpriteProxyNode* lights = new SpriteProxyNode();
      lights->setTexture(ornamentTexture);
      lights->SetLayer(-1);
      lights->SetShader(SHADERS.GetShader(ShaderType::COLORIZE));

      xmasAnim << "lights";
      xmasAnim.Refresh(lights->getSprite());
      new_tree->AddNode(lights);

      this->trees.push_back(new_tree);
      this->lights.push_back(lights);
      map.AddSprite(&*new_tree, 0);

      // do not walk through trees
      auto tile = map.GetTileAt(pos);
      tile.solid = true;
      map.SetTileAt(pos, tile);
    }

    // tree with xmas bulbs
    for (auto pos : treeWBulb) {
      auto new_tree = std::make_shared<SpriteProxyNode>(*treeTexture);
      new_tree->setPosition(pos);

      SpriteProxyNode* bulbs = new SpriteProxyNode();
      bulbs->setTexture(ornamentTexture);
      bulbs->SetLayer(-1);
      xmasAnim << "bulbs";
      xmasAnim.Refresh(bulbs->getSprite());
      new_tree->AddNode(bulbs);

      SpriteProxyNode* lights = new SpriteProxyNode();
      lights->setTexture(ornamentTexture);
      lights->SetLayer(-1);
      lights->SetShader(SHADERS.GetShader(ShaderType::COLORIZE));
      xmasAnim << "lights";
      xmasAnim.Refresh(lights->getSprite());
      new_tree->AddNode(lights);

      map.AddSprite(&*new_tree, 0);

      this->trees.push_back(new_tree);
      this->bulbs.push_back(bulbs);
      this->lights.push_back(lights);

      // do not walk through trees
      auto tile = map.GetTileAt(pos);
      tile.solid = true;
      map.SetTileAt(pos, tile);
    }

    // tree with xmas everything
    for (auto pos : xmasTree) {
      auto new_tree = std::make_shared<SpriteProxyNode>(*treeTexture);
      new_tree->setPosition(pos);

      SpriteProxyNode* bulbs = new SpriteProxyNode();
      bulbs->setTexture(ornamentTexture);
      bulbs->SetLayer(-1);
      bulbs->SetShader(SHADERS.GetShader(ShaderType::GRADIENT));

      // hard-coded yellow to peach from original ribbon sprite
      bulbs->GetShader().SetUniform("firstColor", { 248, 72, 4, 255 });
      bulbs->GetShader().SetUniform("secondColor", { 248, 242, 114, 255 });

      xmasAnim << "bulbs_w_ribbon";
      xmasAnim.Refresh(bulbs->getSprite());
      new_tree->AddNode(bulbs);
      ribbons.push_back(bulbs);

      SpriteProxyNode* lights = new SpriteProxyNode();
      lights->setTexture(ornamentTexture);
      lights->SetLayer(-1);
      lights->SetShader(SHADERS.GetShader(ShaderType::COLORIZE));
      xmasAnim << "lights";
      xmasAnim.Refresh(lights->getSprite());
      new_tree->AddNode(lights);

      SpriteProxyNode* star = new SpriteProxyNode();
      star->setTexture(ornamentTexture);
      star->SetLayer(-1);
      starAnim.Refresh(star->getSprite());
      new_tree->AddNode(star);
      
      auto offset = treeAnim.GetPoint("top") - treeAnim.GetPoint("origin");
      star->setPosition(offset);

      map.AddSprite(&*new_tree, 0);

      this->trees.push_back(new_tree);
      this->bulbs.push_back(bulbs);
      this->stars.push_back(star);
      this->lights.push_back(lights);

      // do not walk through trees
      auto tile = map.GetTileAt(pos);
      tile.solid = true;
      map.SetTileAt(pos, tile);
    }

    // paths (NOTE: hacky for demo)
    int first = 1;
    for (auto pos : paths) {
      pathControllers[0].AddPoint(pos);
      if (first-- > 0) {
        pathControllers[0].AddWait(frames(60));
      }
    }
    pathControllers[0].AddWait(frames(60));

    menuWidget.SetArea(map.GetName());
    this->tiles = result.second;
  }
}

void Overworld::SceneBase::TeleportUponReturn(const sf::Vector2f& position)
{
  teleportedOut = true;
  returnPoint = position;
}

const bool Overworld::SceneBase::HasTeleportedAway() const
{
  return teleportedOut;
}

void Overworld::SceneBase::SetBackground(Background* background)
{
  if (this->bg) {
    delete this->bg;
    this->bg = nullptr;
  }

  this->bg = background;
}

void Overworld::SceneBase::GotoChipFolder()
{
  gotoNextScene = true;
  AUDIO.Play(AudioType::CHIP_DESC);

  using effect = segue<PushIn<direction::left>, milliseconds<500>>;
  getController().push<effect::to<FolderScene>>(folders);
}

void Overworld::SceneBase::GotoNaviSelect()
{
  // Navi select
  gotoNextScene = true;
  AUDIO.Play(AudioType::CHIP_DESC);

  using effect = segue<Checkerboard, milliseconds<250>>;
  getController().push<effect::to<SelectNaviScene>>(currentNavi);
}

void Overworld::SceneBase::GotoConfig()
{
  // Config Select on PC
  gotoNextScene = true;
  AUDIO.Play(AudioType::CHIP_DESC);

  using effect = segue<DiamondTileSwipe<direction::right>, milliseconds<500>>;
  getController().push<effect::to<ConfigScene>>();
}

void Overworld::SceneBase::GotoMobSelect()
{
  gotoNextScene = true;

  CardFolder* folder = nullptr;

  if (folders.GetFolder(0, folder)) {
    using effect = segue<PixelateBlackWashFade, milliseconds<500>>;
    AUDIO.Play(AudioType::CHIP_DESC);
    getController().push<effect::to<SelectMobScene>>(currentNavi, *folder, programAdvance);
  }
  else {
    AUDIO.Play(AudioType::CHIP_ERROR);
    Logger::Log("Cannot proceed to battles. You need 1 folder minimum.");
    gotoNextScene = false;
  }
}

void Overworld::SceneBase::GotoPVP()
{
  gotoNextScene = true;

  CardFolder* folder = nullptr;

  if (folders.GetFolder(0, folder)) {
    AUDIO.Play(AudioType::CHIP_DESC);
    using effect = segue<PushIn<direction::down>, milliseconds<500>>;
    getController().push<effect::to<PVPScene>>(static_cast<int>(currentNavi), *folder, programAdvance);
  }
  else {
    AUDIO.Play(AudioType::CHIP_ERROR);
    Logger::Log("Cannot proceed to battles. You need 1 folder minimum.");
    gotoNextScene = false;
  }
}

Overworld::QuadTree& Overworld::SceneBase::GetQuadTree()
{
  return quadTree;
}

Camera& Overworld::SceneBase::GetCamera()
{
  return camera;
}

Overworld::Map& Overworld::SceneBase::GetMap()
{
  return map;
}

Overworld::Actor& Overworld::SceneBase::GetPlayer()
{
  return playerActor;
}

Overworld::PlayerController& Overworld::SceneBase::GetPlayerController()
{
  return playerController;
}

Overworld::TeleportController& Overworld::SceneBase::GetTeleportControler()
{
  return teleportController;
}

SelectedNavi& Overworld::SceneBase::GetCurrentNavi()
{
  return currentNavi;
}

Background* Overworld::SceneBase::GetBackground()
{
  return this->bg;
}

AnimatedTextBox& Overworld::SceneBase::GetTextBox()
{
  return textbox;
}

#ifdef __ANDROID__
void Overworld::SceneBase::StartupTouchControls() {
    ui.setScale(2.f,2.f);

    uiAnimator.SetAnimation("CHIP_FOLDER_LABEL");
    uiAnimator.SetFrame(1, ui);
    ui.setPosition(100.f, 50.f);

    auto bounds = ui.getGlobalBounds();
    auto rect = sf::IntRect(int(bounds.left), int(bounds.top), int(bounds.width), int(bounds.height));
    auto& folderBtn = TouchArea::create(rect);

    folderBtn.onRelease([this](sf::Vector2i delta) {
        Logger::Log("folder released");

        gotoNextScene = true;
        AUDIO.Play(AudioType::CHIP_DESC);

        using swoosh::intent::direction;
        using segue = swoosh::intent::segue<PushIn<direction::left>, swoosh::intent::milli<500>>;
        getController().push<segue::to<FolderScene>>(data);
    });

    folderBtn.onTouch([this]() {
        menuSelectionIndex = 0;
    });

    uiAnimator.SetAnimation("LIBRARY_LABEL");
    uiAnimator.SetFrame(1, ui);
    ui.setPosition(100.f, 120.f);

    bounds = ui.getGlobalBounds();
    rect = sf::IntRect(int(bounds.left), int(bounds.top), int(bounds.width), int(bounds.height));

    Logger::Log(std::string("rect: ") + std::to_string(rect.left) + ", " + std::to_string(rect.top) + ", " + std::to_string(rect.width) + ", " + std::to_string(rect.height));

    auto& libraryBtn = TouchArea::create(rect);

    libraryBtn.onRelease([this](sf::Vector2i delta) {
        Logger::Log("library released");

        gotoNextScene = true;
        AUDIO.Play(AudioType::CHIP_DESC);

        using swoosh::intent::direction;
        using segue = swoosh::intent::segue<PushIn<direction::right>>;
        getController().push<segue::to<LibraryScene>, swoosh::intent::milli<500>>();
    });

    libraryBtn.onTouch([this]() {
        menuSelectionIndex = 1;
    });


    uiAnimator.SetAnimation("NAVI_LABEL");
    uiAnimator.SetFrame(1, ui);
    ui.setPosition(100.f, 190.f);

    bounds = ui.getGlobalBounds();
    rect = sf::IntRect(int(bounds.left), int(bounds.top), int(bounds.width), int(bounds.height));
    auto& naviBtn = TouchArea::create(rect);

    naviBtn.onRelease([this](sf::Vector2i delta) {
        gotoNextScene = true;
        AUDIO.Play(AudioType::CHIP_DESC);
        using segue = swoosh::intent::segue<Checkerboard, swoosh::intent::milli<500>>;
        using intent = segue::to<SelectNaviScene>;
        getController().push<intent>(currentNavi);
    });

    naviBtn.onTouch([this]() {
        menuSelectionIndex = 2;
    });

    uiAnimator.SetAnimation("MOB_SELECT_LABEL");
    uiAnimator.SetFrame(1, ui);
    ui.setPosition(100.f, 260.f);

    bounds = ui.getGlobalBounds();
    rect = sf::IntRect(int(bounds.left), int(bounds.top), int(bounds.width), int(bounds.height));
    auto& mobBtn = TouchArea::create(rect);

    mobBtn.onRelease([this](sf::Vector2i delta) {
        gotoNextScene = true;

        CardFolder* folder = nullptr;

        if (data.GetFolder("Default", folder)) {
          AUDIO.Play(AudioType::CHIP_DESC);
          using segue = swoosh::intent::segue<PixelateBlackWashFade, swoosh::intent::milli<500>>::to<SelectMobScene>;
          getController().push<segue>(currentNavi, *folder);
        }
        else {
          AUDIO.Play(AudioType::CHIP_ERROR);
          Logger::Log("Cannot proceed to mob select. Error selecting folder 'Default'.");
          gotoNextScene = false;
        }
    });

    mobBtn.onTouch([this]() {
        menuSelectionIndex = 3;
    });
}

void Overworld::SceneBase::ShutdownTouchControls() {
  TouchArea::free();
}
#endif