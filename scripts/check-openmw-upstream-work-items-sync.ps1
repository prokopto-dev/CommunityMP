param(
    [string]$SourceRoot = "",
    [switch]$FailOnMissingGuard
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = "Stop"
$guardCount = 0

if ($SourceRoot -eq "") {
    $SourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
} else {
    $SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
}

function Get-SourceText {
    param([string]$RelativePath)

    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required source file was not found: $path"
    }

    return Get-Content -LiteralPath $path -Raw
}

function Test-Pattern {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Missing
    )

    $script:guardCount++

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        $Missing.Add($Name)
    }
}

$engine = Get-SourceText "apps\openmw\engine.cpp"
$stateManager = Get-SourceText "apps\openmw\mwstate\statemanagerimp.cpp"
$weatherBindings = Get-SourceText "apps\openmw\mwlua\weatherbindings.cpp"
$coreLua = Get-SourceText "files\lua_api\openmw\core.lua"
$coreBindings = Get-SourceText "apps\openmw\mwlua\corebindings.cpp"
$uiApiLua = Get-SourceText "files\lua_api\openmw\ui.lua"
$hud = Get-SourceText "apps\openmw\mwgui\hud.cpp"
$openAlOutput = Get-SourceText "apps\openmw\mwsound\openaloutput.cpp"
$soundManagerImp = Get-SourceText "apps\openmw\mwsound\soundmanagerimp.cpp"
$soundManagerImpHeader = Get-SourceText "apps\openmw\mwsound\soundmanagerimp.hpp"
$soundBufferHeader = Get-SourceText "apps\openmw\mwsound\soundbuffer.hpp"
$ambientLua = Get-SourceText "files\lua_api\openmw\ambient.lua"
$pathUtil = Get-SourceText "components\vfs\pathutil.hpp"
$pathUtilTest = Get-SourceText "apps\components_tests\vfs\testpathutil.cpp"
$luaUiResources = Get-SourceText "components\lua_ui\resources.hpp"
$dataFilesPage = Get-SourceText "apps\launcher\datafilespage.cpp"
$mapSettings = Get-SourceText "components\settings\categories\map.hpp"
$videoSettings = Get-SourceText "components\settings\categories\video.hpp"
$settingsDefault = Get-SourceText "files\settings-default.cfg"
$ripples = Get-SourceText "apps\openmw\mwrender\ripplesimulation.cpp"
$miscConvert = Get-SourceText "components\misc\convert.hpp"
$miscConvertTest = Get-SourceText "apps\components_tests\misc\testconvert.cpp"
$compatGroundcoverFrag = Get-SourceText "files\shaders\compatibility\groundcover.frag"
$compatGroundcoverVert = Get-SourceText "files\shaders\compatibility\groundcover.vert"
$compatObjectsFrag = Get-SourceText "files\shaders\compatibility\objects.frag"
$compatObjectsVert = Get-SourceText "files\shaders\compatibility\objects.vert"
$compatTerrainFrag = Get-SourceText "files\shaders\compatibility\terrain.frag"
$compatTerrainVert = Get-SourceText "files\shaders\compatibility\terrain.vert"
$vertexShaderCore = Get-SourceText "files\shaders\lib\core\vertex.glsl"
$vertexShaderHeader = Get-SourceText "files\shaders\lib\core\vertex.h.glsl"
$localScripts = Get-SourceText "apps\openmw\mwlua\localscripts.cpp"
$localScriptsHeader = Get-SourceText "apps\openmw\mwlua\localscripts.hpp"
$luaManagerBase = Get-SourceText "apps\openmw\mwbase\luamanager.hpp"
$luaManagerImp = Get-SourceText "apps\openmw\mwlua\luamanagerimp.cpp"
$luaManagerImpHeader = Get-SourceText "apps\openmw\mwlua\luamanagerimp.hpp"
$luaWorker = Get-SourceText "apps\openmw\mwlua\worker.cpp"
$luaWorkerHeader = Get-SourceText "apps\openmw\mwlua\worker.hpp"
$profileHeader = Get-SourceText "apps\openmw\profile.hpp"
$luaSettingsDocs = Get-SourceText "docs\source\reference\modding\settings\lua.rst"
$engineEvents = Get-SourceText "apps\openmw\mwlua\engineevents.cpp"
$engineEventsHeader = Get-SourceText "apps\openmw\mwlua\engineevents.hpp"
$dialogueBindings = Get-SourceText "apps\openmw\mwlua\dialoguebindings.cpp"
$animationBindings = Get-SourceText "apps\openmw\mwlua\animationbindings.cpp"
$dialogueInfo = Get-SourceText "apps\openmw\mwlua\dialogueinfo.cpp"
$dialogueInfoHeader = Get-SourceText "apps\openmw\mwlua\dialogueinfo.hpp"
$userdataSerializer = Get-SourceText "apps\openmw\mwlua\userdataserializer.cpp"
$aiLua = Get-SourceText "files\data\scripts\omw\ai.lua"
$uiLua = Get-SourceText "files\data\scripts\omw\ui.lua"
$mechanicsGlobalLua = Get-SourceText "files\data\scripts\omw\mechanics\globalcontroller.lua"
$settingsCommonLua = Get-SourceText "files\data\scripts\omw\settings\common.lua"
$mechanics = Get-SourceText "apps\openmw\mwmechanics\mechanicsmanagerimp.cpp"
$sdlEvents = Get-SourceText "components\sdlutil\events.hpp"
$sdlInputWrapper = Get-SourceText "components\sdlutil\sdlinputwrapper.cpp"
$sdlInputWrapperHeader = Get-SourceText "components\sdlutil\sdlinputwrapper.hpp"
$mouseManager = Get-SourceText "apps\openmw\mwinput\mousemanager.cpp"
$fxPassHeader = Get-SourceText "components\fx\pass.hpp"
$fxTechniqueHeader = Get-SourceText "components\fx\technique.hpp"
$fxTechnique = Get-SourceText "components\fx\technique.cpp"
$postProcessor = Get-SourceText "apps\openmw\mwrender\postprocessor.cpp"
$postProcessorHeader = Get-SourceText "apps\openmw\mwrender\postprocessor.hpp"
$fxTechniqueTest = Get-SourceText "apps\components_tests\fx\technique.cpp"
$quadTreeWorldHeader = Get-SourceText "components\terrain\quadtreeworld.hpp"
$quadTreeWorld = Get-SourceText "components\terrain\quadtreeworld.cpp"
$renderingManagerHeader = Get-SourceText "apps\openmw\mwrender\renderingmanager.hpp"
$renderingManager = Get-SourceText "apps\openmw\mwrender\renderingmanager.cpp"
$cmakeLists = Get-SourceText "CMakeLists.txt"
$openmwCmakeLists = Get-SourceText "apps\openmw\CMakeLists.txt"
$componentsTestsCmake = Get-SourceText "apps\components_tests\CMakeLists.txt"
$filesystemArchive = Get-SourceText "components\vfs\filesystemarchive.cpp"
$filesystemArchiveTest = Get-SourceText "apps\components_tests\vfs\testfilesystemarchive.cpp"
$wineUtils = Get-SourceText "components\files\wineutils.hpp"
$linuxPath = Get-SourceText "components\files\linuxpath.cpp"
$wineUtilsTest = Get-SourceText "apps\components_tests\files\wineutils.cpp"
$cameraBindings = Get-SourceText "apps\openmw\mwlua\camerabindings.cpp"
$cameraRender = Get-SourceText "apps\openmw\mwrender\camera.cpp"
$cameraRenderHeader = Get-SourceText "apps\openmw\mwrender\camera.hpp"
$cameraLua = Get-SourceText "files\lua_api\openmw\camera.lua"
$aiCombat = Get-SourceText "apps\openmw\mwmechanics\aicombat.cpp"
$aiCombatHeader = Get-SourceText "apps\openmw\mwmechanics\aicombat.hpp"
$aiSequence = Get-SourceText "apps\openmw\mwmechanics\aisequence.cpp"
$aiSequenceHeader = Get-SourceText "apps\openmw\mwmechanics\aisequence.hpp"
$esmAiSequence = Get-SourceText "components\esm3\aisequence.cpp"
$esmAiSequenceHeader = Get-SourceText "components\esm3\aisequence.hpp"
$inputBindings = Get-SourceText "apps\openmw\mwlua\inputbindings.cpp"
$inputSettingsLua = Get-SourceText "files\data\scripts\omw\input\settings.lua"
$inputActions = Get-SourceText "components\lua\inputactions.cpp"
$inputActionsHeader = Get-SourceText "components\lua\inputactions.hpp"
$inputActionsTest = Get-SourceText "apps\components_tests\lua\testinputactions.cpp"
$luaState = Get-SourceText "components\lua\luastate.cpp"
$luaStateTest = Get-SourceText "apps\components_tests\lua\testlua.cpp"
$scriptsContainer = Get-SourceText "components\lua\scriptscontainer.cpp"
$scriptsContainerHeader = Get-SourceText "components\lua\scriptscontainer.hpp"
$scriptsContainerTest = Get-SourceText "apps\components_tests\lua\testscriptscontainer.cpp"
$luaUiContent = Get-SourceText "components\lua_ui\content.lua"
$luaUiContentTest = Get-SourceText "apps\components_tests\lua\testuicontent.cpp"
$uiBindings = Get-SourceText "apps\openmw\mwlua\uibindings.cpp"
$luaUiWidget = Get-SourceText "components\lua_ui\widget.cpp"
$luaUiTextEdit = Get-SourceText "components\lua_ui\textedit.cpp"
$luaUiWidgetHeader = Get-SourceText "components\lua_ui\widget.hpp"
$luaUiElement = Get-SourceText "components\lua_ui\element.cpp"
$luaUiUtil = Get-SourceText "components\lua_ui\util.cpp"
$luaUiUtilHeader = Get-SourceText "components\lua_ui\util.hpp"
$ingredientBindings = Get-SourceText "apps\openmw\mwlua\types\ingredient.cpp"
$lockpickBindings = Get-SourceText "apps\openmw\mwlua\types\lockpick.cpp"
$repairBindings = Get-SourceText "apps\openmw\mwlua\types\repair.cpp"
$apparatusBindings = Get-SourceText "apps\openmw\mwlua\types\apparatus.cpp"
$actorBindings = Get-SourceText "apps\openmw\mwlua\types\actor.cpp"
$armorBindings = Get-SourceText "apps\openmw\mwlua\types\armor.cpp"
$clothingBindings = Get-SourceText "apps\openmw\mwlua\types\clothing.cpp"
$containerBindings = Get-SourceText "apps\openmw\mwlua\types\container.cpp"
$creatureBindings = Get-SourceText "apps\openmw\mwlua\types\creature.cpp"
$npcBindings = Get-SourceText "apps\openmw\mwlua\types\npc.cpp"
$weaponBindings = Get-SourceText "apps\openmw\mwlua\types\weapon.cpp"
$contentBindings = Get-SourceText "apps\openmw\mwlua\contentbindings.cpp"
$soundBindings = Get-SourceText "apps\openmw\mwlua\soundbindings.cpp"
$statsExtensions = Get-SourceText "apps\openmw\mwscript\statsextensions.cpp"
$resurrectMechanics = Get-SourceText "apps\openmw\mwmechanics\resurrect.cpp"
$resurrectMechanicsHeader = Get-SourceText "apps\openmw\mwmechanics\resurrect.hpp"
$spellEffects = Get-SourceText "apps\openmw\mwmechanics\spelleffects.cpp"
$contentLua = Get-SourceText "files\lua_api\openmw\content.lua"
$typesLua = Get-SourceText "files\lua_api\openmw\types.lua"
$worldLua = Get-SourceText "files\lua_api\openmw\world.lua"
$luaApiGlobalTest = Get-SourceText "scripts\data\integration_tests\test_lua_api\global.lua"
$luaApiLoadTest = Get-SourceText "scripts\data\integration_tests\test_lua_api\load.lua"
$luaApiPlayerTest = Get-SourceText "scripts\data\integration_tests\test_lua_api\player.lua"
$luaApiMenuTest = Get-SourceText "scripts\data\integration_tests\test_lua_api\menu.lua"
$windowManagerBase = Get-SourceText "apps\openmw\mwbase\windowmanager.hpp"
$windowBaseHeader = Get-SourceText "apps\openmw\mwgui\windowbase.hpp"
$windowManagerImpHeader = Get-SourceText "apps\openmw\mwgui\windowmanagerimp.hpp"
$windowManagerImp = Get-SourceText "apps\openmw\mwgui\windowmanagerimp.cpp"
$inventoryWindow = Get-SourceText "apps\openmw\mwgui\inventorywindow.cpp"
$containerWindow = Get-SourceText "apps\openmw\mwgui\container.cpp"
$companionWindow = Get-SourceText "apps\openmw\mwgui\companionwindow.cpp"
$trainingWindowHeader = Get-SourceText "apps\openmw\mwgui\trainingwindow.hpp"
$trainingWindow = Get-SourceText "apps\openmw\mwgui\trainingwindow.cpp"
$saveGameDialogHeader = Get-SourceText "apps\openmw\mwgui\savegamedialog.hpp"
$saveGameDialog = Get-SourceText "apps\openmw\mwgui\savegamedialog.cpp"
$dialogueManager = Get-SourceText "apps\openmw\mwdialogue\dialoguemanagerimp.cpp"
$dialogueWindow = Get-SourceText "apps\openmw\mwgui\dialogue.cpp"
$dialogueLayout = Get-SourceText "files\data\mygui\openmw_dialogue_window.layout"
$saveGameDialogLayout = Get-SourceText "files\data\mygui\openmw_savegame_dialog.layout"
$listSkin = Get-SourceText "files\data\mygui\openmw_list.skin.xml"
$widgetsListHeader = Get-SourceText "components\widgets\list.hpp"
$widgetsList = Get-SourceText "components\widgets\list.cpp"
$worldClass = Get-SourceText "apps\openmw\mwworld\class.cpp"
$worldClassHeader = Get-SourceText "apps\openmw\mwworld\class.hpp"
$npcClass = Get-SourceText "apps\openmw\mwclass\npc.cpp"
$npcClassHeader = Get-SourceText "apps\openmw\mwclass\npc.hpp"
$combatMechanics = Get-SourceText "apps\openmw\mwmechanics\combat.cpp"
$spellCasting = Get-SourceText "apps\openmw\mwmechanics\spellcasting.cpp"
$activeSpells = Get-SourceText "apps\openmw\mwmechanics\activespells.cpp"
$magicBindings = Get-SourceText "apps\openmw\mwlua\magicbindings.cpp"
$securityMechanics = Get-SourceText "apps\openmw\mwmechanics\security.cpp"
$pickpocketModel = Get-SourceText "apps\openmw\mwgui\pickpocketitemmodel.cpp"
$skillHandlersLua = Get-SourceText "files\data\scripts\omw\skillhandlers.lua"
$actionBindingsLua = Get-SourceText "files\data\scripts\omw\input\actionbindings.lua"
$inputManagerBase = Get-SourceText "apps\openmw\mwbase\inputmanager.hpp"
$inputManagerImpHeader = Get-SourceText "apps\openmw\mwinput\inputmanagerimp.hpp"
$inputManagerImp = Get-SourceText "apps\openmw\mwinput\inputmanagerimp.cpp"
$keyboardManager = Get-SourceText "apps\openmw\mwinput\keyboardmanager.cpp"
$controllerManagerHeader = Get-SourceText "apps\openmw\mwinput\controllermanager.hpp"
$controllerManager = Get-SourceText "apps\openmw\mwinput\controllermanager.cpp"
$worldBase = Get-SourceText "apps\openmw\mwbase\world.hpp"
$worldImpHeader = Get-SourceText "apps\openmw\mwworld\worldimp.hpp"
$worldImp = Get-SourceText "apps\openmw\mwworld\worldimp.cpp"
$worldScene = Get-SourceText "apps\openmw\mwworld\scene.cpp"
$compilerExtensions = Get-SourceText "components\compiler\extensions0.cpp"
$compilerOpcodes = Get-SourceText "components\compiler\opcodes.hpp"
$controlExtensions = Get-SourceText "apps\openmw\mwscript\controlextensions.cpp"
$transformationExtensions = Get-SourceText "apps\openmw\mwscript\transformationextensions.cpp"
$miscExtensions = Get-SourceText "apps\openmw\mwscript\miscextensions.cpp"
$physicsSystemHeader = Get-SourceText "apps\openmw\mwphysics\physicssystem.hpp"
$physicsSystem = Get-SourceText "apps\openmw\mwphysics\physicssystem.cpp"
$physicsActorHeader = Get-SourceText "apps\openmw\mwphysics\actor.hpp"
$physicsActor = Get-SourceText "apps\openmw\mwphysics\actor.cpp"
$movementSolver = Get-SourceText "apps\openmw\mwphysics\movementsolver.cpp"
$actorConvexCallback = Get-SourceText "apps\openmw\mwphysics\actorconvexcallback.cpp"
$openCsTools = Get-SourceText "apps\opencs\model\tools\tools.cpp"
$openCsRecordIdCheckHeader = Get-SourceText "apps\opencs\model\tools\recordidcheck.hpp"
$openCsRecordIdCheck = Get-SourceText "apps\opencs\model\tools\recordidcheck.cpp"
$openCsRecordIdCheckTest = Get-SourceText "apps\opencs_tests\model\tools\testrecordidcheck.cpp"
$mwCellRef = Get-SourceText "apps\openmw\mwworld\cellref.cpp"
$mwrenderAnimation = Get-SourceText "apps\openmw\mwrender\animation.cpp"
$mwrenderEffectManager = Get-SourceText "apps\openmw\mwrender\effectmanager.cpp"
$mwrenderUtilHeader = Get-SourceText "apps\openmw\mwrender\util.hpp"
$mwrenderUtil = Get-SourceText "apps\openmw\mwrender\util.cpp"
$nifOsgParticleHeader = Get-SourceText "components\nifosg\particle.hpp"
$nifOsgParticle = Get-SourceText "components\nifosg\particle.cpp"
$nifOsgParticleTest = Get-SourceText "apps\components_tests\nifosg\testparticle.cpp"
$nifNodeHeader = Get-SourceText "components\nif\node.hpp"
$nifNode = Get-SourceText "components\nif\node.cpp"
$nifLoader = Get-SourceText "components\nifosg\nifloader.cpp"
$nifOsgLoaderTest = Get-SourceText "apps\components_tests\nifosg\testnifloader.cpp"
$sceneUtilOptimizer = Get-SourceText "components\sceneutil\optimizer.cpp"
$bulletNifLoaderHeader = Get-SourceText "components\nifbullet\bulletnifloader.hpp"
$bulletNifLoader = Get-SourceText "components\nifbullet\bulletnifloader.cpp"
$bulletNifLoaderTest = Get-SourceText "apps\components_tests\nifloader\testbulletnifloader.cpp"
$esmCellRef = Get-SourceText "components\esm3\cellref.cpp"
$loadInfo = Get-SourceText "components\esm3\loadinfo.cpp"
$esm3SaveLoadTest = Get-SourceText "apps\components_tests\esm3\testsaveload.cpp"
$opencsView = Get-SourceText "apps\opencs\view\doc\view.cpp"
$opencsPrefsState = Get-SourceText "apps\opencs\model\prefs\state.cpp"
$opencsCommandsHeader = Get-SourceText "apps\opencs\model\world\commands.hpp"
$opencsCommands = Get-SourceText "apps\opencs\model\world\commands.cpp"
$opencsScriptSubViewHeader = Get-SourceText "apps\opencs\view\world\scriptsubview.hpp"
$opencsScriptSubView = Get-SourceText "apps\opencs\view\world\scriptsubview.cpp"
$opencsDialogueSubViewHeader = Get-SourceText "apps\opencs\view\world\dialoguesubview.hpp"
$opencsDialogueSubView = Get-SourceText "apps\opencs\view\world\dialoguesubview.cpp"
$opencsSceneWidget = Get-SourceText "apps\opencs\view\render\scenewidget.cpp"
$opencsObjectMarker = Get-SourceText "apps\opencs\view\render\objectmarker.cpp"
$opencsCellArrow = Get-SourceText "apps\opencs\view\render\cellarrow.cpp"
$opencsPathgrid = Get-SourceText "apps\opencs\view\render\pathgrid.cpp"
$opencsWorldspaceWidgetHeader = Get-SourceText "apps\opencs\view\render\worldspacewidget.hpp"
$opencsWorldspaceWidget = Get-SourceText "apps\opencs\view\render\worldspacewidget.cpp"
$opencsPagedWorldspaceWidget = Get-SourceText "apps\opencs\view\render\pagedworldspacewidget.cpp"
$opencsFileDialog = Get-SourceText "apps\opencs\view\doc\filedialog.cpp"
$opencsColumnBaseHeader = Get-SourceText "apps\opencs\model\world\columnbase.hpp"
$opencsColumnBase = Get-SourceText "apps\opencs\model\world\columnbase.cpp"
$opencsColumnImpHeader = Get-SourceText "apps\opencs\model\world\columnimp.hpp"
$opencsIdCompletionManager = Get-SourceText "apps\opencs\model\world\idcompletionmanager.cpp"
$opencsIdCompletionDelegate = Get-SourceText "apps\opencs\view\world\idcompletiondelegate.cpp"
$opencsTableMimeData = Get-SourceText "apps\opencs\model\world\tablemimedata.cpp"
$opencsTestsCmake = Get-SourceText "apps\opencs_tests\CMakeLists.txt"
$opencsIdCompletionManagerTest = Get-SourceText "apps\opencs_tests\model\world\testidcompletionmanager.cpp"
$toutf8 = Get-SourceText "components\toutf8\toutf8.cpp"
$toutf8Test = Get-SourceText "apps\components_tests\toutf8\toutf8.cpp"
$esmWriter = Get-SourceText "components\esm3\esmwriter.cpp"
$esmWriterTest = Get-SourceText "apps\components_tests\esm3\testesmwriter.cpp"

$missing = [System.Collections.Generic.List[string]]::new()

Test-Pattern -Name "OpenMW #9145 close requests are processed during initial and save loading" -Text ($engine + "`n" + $stateManager) `
    -Pattern 'while\s*\(dataLoading\.wait_for\(50ms\)\s*!=\s*std::future_status::ready\).*asyncListener\.update\(\);.*mInputManager->update\(0\.f,\s*true,\s*true\);.*if\s*\(mStateManager->hasQuitRequest\(\)\)\s*return;.*void\s+MWState::StateManager::loadGame.*while\s*\(reader\.hasMoreRecs\(\)\).*listener\.increaseProgress\(progressPercent\s*-\s*currentPercent\).*if\s*\(hasQuitRequest\(\)\)\s*\{\s*cleanup\(true\);\s*return;\s*\}.*mCharacterManager\.setCurrentCharacter\(character\);' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9147 Lua active spell additions apply at zero delta while paused" -Text ($activeSpells + "`n" + $magicBindings) `
    -Pattern '(?=.*activeSpellsT\["add"\].*params\.setFlag\(ESM::ActiveSpells::Flag_Lua\).*store->addSpell\(params\))(?=.*const\s+bool\s+shouldApply\s*=\s*context\.mUpdate\s*\|\|\s*params\.hasFlag\(ESM::ActiveSpells::Flag_Lua\);.*if\s*\(shouldApply\s*&&\s*updateActiveSpell\(ptr,\s*0\.f,\s*it,\s*context\)\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9146 Lua self-targeted health effects do not show enemy health bar" -Text ($magicBindings + "`n" + $spellCasting) `
    -Pattern 'activeSpellsT\["add"\].*const\s+MWWorld::Ptr\s+target\s*=\s*spells\.mActor\.ptr\(\);.*const\s+bool\s+castByPlayer\s*=\s*!casterPtr\.isEmpty\(\)\s*&&\s*casterPtr\s*==\s*MWMechanics::getPlayer\(\);.*targetIsDeadActor\s*=\s*targetStats\.isDead\(\)\s*&&\s*targetStats\.isDeathAnimationFinished\(\);.*if\s*\(affectsHealth\s*&&\s*castByPlayer\s*&&\s*target\s*!=\s*casterPtr\s*&&\s*targetIsActor\s*&&\s*!targetIsDeadActor\).*setEnemy\(target\).*if\s*\(castByPlayer\s*&&\s*target\s*!=\s*mCaster\s*&&\s*targetIsActor\s*&&\s*!targetIsDeadActor\s*&&\s*effectAffectsHealth\).*setEnemy\(target\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9144 exposes Masser and Secunda phase to Lua docs and bindings" -Text ($weatherBindings + "`n" + $coreLua) `
    -Pattern 'api\["getMasserPhase"\].*getWorld\(\)->getMasserPhase\(\).*api\["getSecundaPhase"\].*getWorld\(\)->getSecundaPhase\(\).*@function \[parent=#Weather\] getMasserPhase.*@function \[parent=#Weather\] getSecundaPhase' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9110 default audio device polling avoids holding reopen lock" -Text $openAlOutput `
    -Pattern 'std::basic_string<ALCchar>\s+getCurrentName\(\)\s+const.*void\s+setCurrentName\(std::basic_string_view<ALCchar>\s+name\).*const\s+std::basic_string<ALCchar>\s+currentName\s*=\s*getCurrentName\(\);.*const\s+std::basic_string<ALCchar>\s+defaultName\(getDeviceName\(nullptr\)\);.*if\s*\(currentName\s*!=\s*defaultName\).*std::lock_guard<std::mutex>\s+openLock\(mOutput\.mReopenMutex\).*alcReopenDeviceSOFT\(\s*mOutput\.mDevice,\s*defaultName\.data\(\),\s*mOutput\.mContextAttributes\.data\(\)\).*mCondVar\.wait_for\(lock,\s*std::chrono::seconds\(2\),\s*\[this\]\s*\{\s*return\s+mQuitNow\.load\(\);\s*\}\).*mDefaultDeviceThread->setCurrentName\(getDeviceName\(mDevice\)\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8905 Lua sound file queries include active say streams" -Text ($soundManagerImpHeader + "`n" + $soundManagerImp + "`n" + $coreLua + "`n" + $ambientLua) `
    -Pattern '(?=.*struct\s+SaySound\s*\{.*const\s+MWWorld::CellStore\*\s+mCell;.*VFS::Path::Normalized\s+mFileName;.*StreamPtr\s+mStream;)(?=.*mSaySoundsQueue\.emplace\(ptr\.mRef,\s*SaySound\{\s*ptr\.mCell,\s*VFS::Path::Normalized\(filename\),\s*std::move\(sound\)\s*\}\);)(?=.*mActiveSaySounds\.emplace\(nullptr,\s*SaySound\{\s*nullptr,\s*VFS::Path::Normalized\(filename\),\s*std::move\(sound\)\s*\}\);)(?=.*const\s+auto\s+isPlayingSaySound\s*=\s*\[this,\s*fileName\]\(const\s+SaySoundMap&\s+sounds,\s*const\s+MWWorld::ConstPtr&\s+ptr\).*snditer->second\.mFileName\s*==\s*fileName.*mOutput->isStreamPlaying\(snditer->second\.mStream\.get\(\)\))(?=.*isPlayingSaySound\(mSaySoundsQueue,\s*ptr\)\s*\|\|\s*isPlayingSaySound\(mActiveSaySounds,\s*ptr\))(?=.*Also returns true for active `core\.sound\.say` voiceover streams with the same file name\.)(?=.*Also returns true for active `ambient\.say` voiceover streams with the same file name\.)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8735 attached 3D sounds are culled when their ref becomes invalid" -Text $soundManagerImp `
    -Pattern '(?=.*const\s+MWWorld::LiveCellRefBase\*\s+attachedRef\s*=\s*snditer->first;)(?=.*attachedRef\s*!=\s*nullptr\s*&&\s*\(attachedRef->isDeleted\(\)\s*\|\|\s*!attachedRef->mData\.isEnabled\(\)\))(?=.*for\s*\(SoundBufferRefPair&\s+sndidx\s*:\s*snditer->second\.mList\).*mOutput->finishSound\(sound\).*mSoundBuffers\.release\(\*sndidx\.second\).*snditer\s*=\s*mActiveSounds\.erase\(snditer\);\s*continue;)(?=.*MWWorld::ConstPtr\s+ptr\s*=\s*attachedRef;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7862 exposes Lua log levels" -Text ($cmakeLists + "`n" + $coreBindings + "`n" + $coreLua + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*api\["LOG_LEVEL"\].*Error.*Warning.*Info.*Verbose.*Debug)(?=.*api\["log"\]\s*=\s*writeLuaLogMessage;)(?=.*void\s+writeLuaLogMessage\(sol::this_state\s+state,\s*int\s+level,\s*sol::variadic_args\s+args\).*Invalid log level.*Log\(static_cast<Debug::Level>\(level\)\)\s*<<\s*formatLuaLogMessage\(state,\s*args\);)(?=.*@field \[parent=#core\] #LogLevel LOG_LEVEL)(?=.*@function \[parent=#core\] log)(?=.*testing\.registerGlobalTest\(''lua log levels''.*core\.LOG_LEVEL\.Warning.*core\.log\(core\.LOG_LEVEL\.Warning.*core\.LOG_LEVEL\.Verbose.*core\.LOG_LEVEL\.Debug.*Invalid log level)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8960 exposes Lua UI drag-and-drop passthrough building blocks" -Text ($cmakeLists + "`n" + $windowManagerBase + "`n" + $windowManagerImpHeader + "`n" + $windowManagerImp + "`n" + $uiBindings + "`n" + $luaUiWidget + "`n" + $luaUiTextEdit + "`n" + $uiApiLua + "`n" + $luaApiPlayerTest + "`n" + $luaApiGlobalTest + "`n" + $luaApiMenuTest) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*virtual\s+bool\s+isDragDropActive\(\)\s+const\s*=\s*0;)(?=.*bool\s+isDragDropActive\(\)\s+const\s+override;)(?=.*bool\s+WindowManager::isDragDropActive\(\)\s+const.*mDragAndDrop\s*&&\s*mDragAndDrop->mIsOnDragAndDrop)(?=.*api\["isDragDropActive"\]\s*=\s*\[windowManager\]\(\).*windowManager->isDragDropActive\(\))(?=(?:.*setNeedMouseFocus\(propertyValue\("interactive",\s*true\)\)){2})(?=.*"interactive")(?=.*@function \[parent=#ui\] isDragDropActive)(?=.*`interactive` - If false, the widget ignores mouse interactions\.)(?=.*testing\.registerLocalTest\(''ui drag and drop state is exposed''.*ui\.isDragDropActive\(\).*interactive\s*=\s*false)(?=.*registerPlayerTest\(''ui drag and drop state is exposed''\))(?=.*registerGlobalTest\(''ui drag and drop state is exposed'')' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8953 Lua can disable native item drag-and-drop" -Text ($cmakeLists + "`n" + $windowManagerBase + "`n" + $windowManagerImpHeader + "`n" + $windowManagerImp + "`n" + $uiBindings + "`n" + $inventoryWindow + "`n" + $containerWindow + "`n" + $companionWindow + "`n" + $uiApiLua + "`n" + $luaApiPlayerTest) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*virtual\s+void\s+setItemDragDropEnabled\(bool\s+enabled\)\s*=\s*0;)(?=.*virtual\s+bool\s+isItemDragDropEnabled\(\)\s+const\s*=\s*0;)(?=.*bool\s+mItemDragDropEnabled;)(?=.*mItemDragDropEnabled\(true\))(?=.*void\s+WindowManager::setItemDragDropEnabled\(bool\s+enabled\).*mItemDragDropEnabled\s*=\s*enabled;.*!mItemDragDropEnabled.*mDragAndDrop->finish\(\))(?=.*api\["setNativeItemDragDropEnabled"\].*setItemDragDropEnabled\(enabled\))(?=.*api\["isNativeItemDragDropEnabled"\].*isItemDragDropEnabled\(\))(?=.*void\s+InventoryWindow::dragItem.*isItemDragDropEnabled\(\).*return;)(?=.*void\s+ContainerWindow::dragItem.*isItemDragDropEnabled\(\).*return;)(?=.*bool\s+ContainerWindow::dragItemByPtr.*isItemDragDropEnabled\(\).*return\s+false;)(?=.*void\s+CompanionWindow::dragItem.*isItemDragDropEnabled\(\).*return;)(?=.*@function \[parent=#ui\] setNativeItemDragDropEnabled)(?=.*@function \[parent=#ui\] isNativeItemDragDropEnabled)(?=.*ui\.setNativeItemDragDropEnabled\(false\).*ui\.isNativeItemDragDropEnabled\(\),\s*false.*ui\.setNativeItemDragDropEnabled\(true\).*ui\.isNativeItemDragDropEnabled\(\),\s*true)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8322 nested NCO string extra data disables generated subtree collision" -Text ($bulletNifLoaderHeader + "`n" + $bulletNifLoader + "`n" + $bulletNifLoaderTest) `
    -Pattern '(?=.*bool\s+mUseStringExtraData\{\s*false\s*\};)(?=.*bool\s+mNoCollision\{\s*false\s*\};)(?=.*void\s+handleStringExtraData\(const\s+Nif::NiAVObject&\s+node,\s+HandleNodeArgs&\s+args\);)(?=.*args\.mUseStringExtraData\s*=\s*true;)(?=.*handleStringExtraData\(node,\s*args\);)(?=.*args\.mUseStringExtraData\s*&&\s*!args\.mRoot.*handleStringExtraData\(node,\s*args\).*if\s*\(args\.mNoCollision\)\s*return;)(?=.*dynamic_cast<const\s+Nif::NiNode\*>\(&node\)\s*==\s*nullptr)(?=.*Misc::StringUtils::ciStartsWith\(sd->mData,\s*"NC"\).*args\.mRoot.*Resource::VisualCollisionType::Default.*else\s*\{\s*args\.mNoCollision\s*=\s*true;)(?=.*for_nested_node_with_extra_data_string_starting_with_nc_should_skip_subtree)(?=.*for_nested_node_with_extra_data_string_starting_with_nc_should_keep_sibling_collision)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8304 bakes non-orthogonal static collision mesh transforms" -Text ($bulletNifLoader + "`n" + $bulletNifLoaderTest) `
    -Pattern '(?=.*#include\s+<components/bullethelpers/processtrianglecallback\.hpp>)(?=.*std::unique_ptr<btCollisionShape>\s+bakeTriangleMeshTransform\(.*const\s+btBvhTriangleMeshShape&\s+shape,\s*const\s+osg::Matrixf&\s+transform\).*makeProcessTriangleCallback.*transform\.preMult\(toOsg\(triangle\[0\]\)\).*shape\.processAllTriangles)(?=.*const\s+bool\s+bakeTransform\s*=\s*!args\.mAnimated\s*&&\s*childShape->getShapeType\(\)\s*==\s*TRIANGLE_MESH_SHAPE_PROXYTYPE\s*&&\s*!transform\.isIdentity\(\);)(?=.*if\s*\(bakeTransform\).*bakeTriangleMeshTransform\(static_cast<const\s+btBvhTriangleMeshShape&>\(\*childShape\),\s*transform\).*transform\s*=\s*osg::Matrixf::identity\(\);)(?=.*should_bake_non_orthogonal_static_mesh_transform_into_triangle_vertices)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8195 bakes ordinary transformed static collision meshes" -Text ($bulletNifLoader + "`n" + $bulletNifLoaderTest) `
    -Pattern '(?=.*const\s+bool\s+bakeTransform\s*=\s*!args\.mAnimated\s*&&\s*childShape->getShapeType\(\)\s*==\s*TRIANGLE_MESH_SHAPE_PROXYTYPE\s*&&\s*!transform\.isIdentity\(\);)(?=.*transform\s*=\s*osg::Matrixf::identity\(\);)(?=.*should_bake_orthogonal_static_mesh_transform_into_triangle_vertices.*osg::Matrixf::rotate.*transform\.preMultTranslate.*shape->addChildShape\(btTransform::getIdentity\(\),\s*mesh\.release\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8895 dialogue result scripts save CRLF line endings" -Text ($loadInfo + "`n" + $esm3SaveLoadTest) `
    -Pattern '(?=.*std::string\s+normalizeDialogueResultScriptLineEndings\(std::string_view\s+value\).*if\s*\(c\s*==\s*''\\r''\).*result\s*\+=\s*"\\r\\n";.*else\s+if\s*\(c\s*==\s*''\\n''\)\s*result\s*\+=\s*"\\r\\n";)(?=.*esm\.writeHNOString\("BNAM",\s*normalizeDialogueResultScriptLineEndings\(mResultScript\)\);)(?=.*infoResultScriptShouldUseWindowsLineEndings.*CLEARINFOACTOR ;\\nChoice Continue 1 ;\\rGoodbye ;\\r\\n.*CLEARINFOACTOR ;\\r\\nChoice Continue 1 ;\\r\\nGoodbye ;\\r\\n)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7451 OpenMW-CS legacy text saves replace unsupported UTF-8" -Text ($toutf8 + "`n" + $esmWriter + "`n" + $toutf8Test + "`n" + $esmWriterTest) `
    -Pattern '(?=.*sReplacementGlyph\s*=\s*''\?'';)(?=.*int\s+getUtf8SequenceLength\(unsigned\s+char\s+value\).*value\s*>=\s*0xf0\s*&&\s*value\s*<=\s*0xf4.*return\s+4;)(?=.*void\s+StatelessUtf8Encoder::copyFromArrayLegacyEnc.*Could not find glyph.*sReplacementGlyph)(?=.*void\s+ESMWriter::writeHNString\(NAME\s+name,\s*std::string_view\s+data,\s*size_t\s+size\).*mEncoder\s*!=\s*nullptr\s*\?\s*mEncoder->getLegacyEnc\(data\)\s*:\s*data.*paddingOffset\s*=\s*data\.empty\(\)\s*\?\s*data\.size\(\)\s*:\s*string\.size\(\))(?=.*getLegacyEncShouldReplaceUnsupportedUtf8Characters.*Fargoth\\xf0\\x9f\\x91\\x8d.*Fargoth\? \?)(?=.*writeHStringShouldEncodeUnsupportedUtf8AsLegacyReplacement.*Fargoth\\xf0\\x9f\\x91\\x8d.*Fargoth\?)(?=.*writeFixedHStringShouldPadAfterLegacyEncoding.*Caf\\xc3\\xa9.*Caf\\xe9\\0\\0\\0\\0)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8890 default normalized VFS path hash and equality support heterogeneous lookup" -Text ($pathUtil + "`n" + $pathUtilTest + "`n" + $luaUiResources + "`n" + $dataFilesPage + "`n" + $soundBufferHeader + "`n" + $postProcessorHeader) `
    -Pattern '(?=.*struct\s+Hash\s*\{.*using\s+is_transparent\s*=\s*void;.*operator\(\)\(std::string_view\s+sv\).*operator\(\)\(const\s+Normalized&\s+s\).*operator\(\)\(NormalizedView\s+s\))(?=.*struct\s+Equal\s*\{.*using\s+is_transparent\s*=\s*void;.*operator\(\)\(const\s+Lhs&\s+lhs,\s*const\s+Rhs&\s+rhs\).*lhs\s*==\s*rhs)(?=.*struct\s+std::hash<VFS::Path::Normalized>\s*:\s*VFS::Path::Hash)(?=.*struct\s+std::hash<VFS::Path::NormalizedView>\s*:\s*VFS::Path::Hash)(?=.*struct\s+std::equal_to<VFS::Path::Normalized>\s*:\s*VFS::Path::Equal)(?=.*struct\s+std::equal_to<VFS::Path::NormalizedView>\s*:\s*VFS::Path::Equal)(?=.*std::unordered_map<Normalized,\s*int>\s+values;.*values\.find\(NormalizedView\("meshes/example\.nif"\)\).*values\.contains\(std::string_view\("meshes/example\.nif"\)\).*std::unordered_set<Normalized>\s+paths;.*paths\.contains\(NormalizedView\("textures/example\.dds"\)\))(?=.*std::unordered_map<VFS::Path::Normalized,\s*TextureResources>\s+mTextures;)(?=.*std::unordered_set<VFS::Path::Normalized>\s+archives;)(?=.*std::unordered_map<VFS::Path::Normalized,\s*SoundBuffer\*>\s+mBufferFileNameMap;)(?=.*std::unordered_set<VFS::Path::Normalized>\s+mTechniqueFiles;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9021 can cap frame rate while the window is unfocused" -Text ($videoSettings + "`n" + $settingsDefault + "`n" + $windowManagerBase + "`n" + $windowManagerImpHeader + "`n" + $windowManagerImp + "`n" + $engine) `
    -Pattern '(?=.*mFramerateLimitOnFocusLoss.*"framerate limit on focus loss".*makeMaxSanitizerFloat\(0\))(?=.*framerate limit on focus loss\s*=\s*0)(?=.*virtual\s+bool\s+isWindowFocused\(\)\s+const\s*=\s*0;)(?=.*bool\s+isWindowFocused\(\)\s+const\s+override;)(?=.*bool\s+mWindowFocused;)(?=.*mWindowFocused\(true\))(?=.*bool\s+WindowManager::isWindowFocused\(\)\s+const\s*\{\s*return\s+mWindowFocused;\s*\})(?=.*void\s+WindowManager::windowFocusChange\(bool\s+focused\).*mWindowFocused\s*=\s*focused;)(?=.*Settings::video\(\)\.mFramerateLimitOnFocusLoss)(?=.*!mWindowManager->isWindowFocused\(\))(?=.*effectiveFrameRateLimit\s*!=\s*frameRateLimit.*Misc::makeFrameRateLimiter\(frameRateLimit\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9034 post-processing render target lookups avoid temporary string insertion" -Text ($fxPassHeader + "`n" + $fxTechniqueHeader + "`n" + $fxTechnique + "`n" + $postProcessor + "`n" + $fxTechniqueTest) `
    -Pattern '(?=.*using\s+RenderTargetMap\s*=\s*std::unordered_map<std::string_view,\s*Types::RenderTarget>;)(?=.*std::string_view\s+getTarget\(\)\s+const\s*\{\s*return\s+mTarget;\s*\})(?=.*for\s*\(std::string_view\s+renderTargetName\s*:\s*it->second->mRenderTargets\).*renderTargetName\.empty\(\).*mRenderTargets\.find\(renderTargetName\).*render target.*not defined)(?=.*technique->getRenderTargetsMap\(\)\.find\(pass->getTarget\(\)\).*rtIt\s*==\s*technique->getRenderTargetsMap\(\)\.end\(\).*continue;)(?=.*technique->getRenderTargetsMap\(\)\.find\(name\).*rtIt\s*==\s*technique->getRenderTargetsMap\(\)\.end\(\).*continue;)(?=.*missing_render_target_input.*render target ''missingtarget'' not defined)(?!.*FIXME:\s*https://gitlab\.com/OpenMW/openmw/-/work_items/9034)(?!.*std::string\s+target\s*=\s*pass->getTarget\(\);.*getRenderTargetsMap\(\)\[target\])' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9088 named world VFX loop by default until explicitly removed" -Text ($animationBindings + "`n" + $worldLua) `
    -Pattern '(?=.*std::string\s+vfxId\s*=\s*options->get_or<std::string>\("vfxId",\s*""\);.*bool\s+loop\s*=\s*options->get<sol::optional<bool>>\("loop"\)\.value_or\(!vfxId\.empty\(\)\);)(?=.*`loop` - boolean, if true the effect will loop until removed \(default: true when `vfxId` is provided, otherwise false\)\.)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9043 parallelizes Lua garbage collection on the Lua worker" -Text ($engine + "`n" + $luaManagerImpHeader + "`n" + $luaManagerImp + "`n" + $luaWorkerHeader + "`n" + $luaWorker + "`n" + $profileHeader + "`n" + $settingsDefault + "`n" + $luaSettingsDocs) `
    -Pattern '(?=.*LuaGc)(?=.*UserStatsValue<UserStatsType::LuaGc>::sValue\{\s*"LuaGC",\s*"luagc"\s*\})(?=.*bool\s+gc\(\);)(?=.*bool\s+LuaManager::gc\(\).*Settings::lua\(\)\.mGcStepsPerFrame.*lua_gc\(mLua\.unsafeState\(\),\s*LUA_GCSTEP,\s*steps\).*return\s+true;)(?=.*enum\s+class\s+Operation\s*\{\s*Gc,\s*Update,\s*\};)(?=.*void\s+gc\(osg::Timer_t\s+frameStart,\s*unsigned\s+int\s+frameNumber,\s*osg::Stats&\s+stats\);)(?=.*void\s+finishGc\(osg::Timer_t\s+frameStart,\s*unsigned\s+int\s+frameNumber,\s*osg::Stats&\s+stats\);)(?=.*mRequest\s*=\s*Request\{\s*\.mOperation\s*=\s*Operation::Gc)(?=.*mGcStopRequest\s*=\s*true;.*mCV\.wait\(lk,\s*\[&\]\s*\{\s*return\s+!mRequest\.has_value\(\);\s*\}\);)(?=.*void\s+Worker::collectGarbage.*OMW::ScopedProfile<OMW::UserStatsType::LuaGc>.*mManager\.gc\(\).*isGcStopRequested\(\))(?=.*case\s+Operation::Gc:\s*collectGarbage)(?=.*mLuaWorker->gc\(frameStart,\s*frameNumber,\s*\*stats\);.*mStereoManager->updateSettings.*mViewer->eventTraversal\(\);.*mViewer->updateTraversal\(\);.*mWorld->updateFocusObject\(\);.*mLuaWorker->finishGc\(frameStart,\s*frameNumber,\s*\*stats\);.*mLuaWorker->allowUpdate\(frameStart,\s*frameNumber,\s*\*stats\);.*mViewer->renderingTraversals\(\);.*mLuaWorker->finishUpdate\(frameStart,\s*frameNumber,\s*\*stats\);)(?=.*Lua garbage collector step granularity per frame)(?=.*repeats steps while the main thread is busy with non-Lua engine work)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9022 interaction raycasts do not load groundcover chunks" -Text ($quadTreeWorldHeader + "`n" + $quadTreeWorld + "`n" + $renderingManager) `
    -Pattern '(?=.*void\s+loadRenderingNode\(ViewDataEntry&\s+entry,\s*ViewData\*\s+vd,\s*float\s+cellWorldSize,\s*const\s+osg::Vec4i&\s+gridbounds,\s*bool\s+compile,\s*unsigned\s+int\s+traversalMask\))(?=.*const\s+unsigned\s+int\s+nodeMask\s*=\s*m->getNodeMask\(\);.*if\s*\(nodeMask\s*!=\s*0\s*&&\s*!\(nodeMask\s*&\s*traversalMask\)\)\s*continue;.*m->getChunk)(?=.*loadRenderingNode\(entry,\s*vd,\s*cellWorldSize,\s*mActiveGrid,\s*false,\s*nv\.getTraversalMask\(\)\))(?=.*loadRenderingNode\(entry,\s*vd,\s*cellWorldSize,\s*grid,\s*true,\s*~0u\))(?=.*mask\s*&=\s*~\(Mask_RenderToTexture\s*\|\s*Mask_Sky\s*\|\s*Mask_Debug\s*\|\s*Mask_Effect\s*\|\s*Mask_Water\s*\|\s*Mask_SimpleWater\s*\|\s*Mask_Groundcover\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8822 default interaction raycasts ignore terrain without changing generic render rays" -Text ($renderingManagerHeader + "`n" + $renderingManager + "`n" + $worldImp) `
    -Pattern '(?=.*RayResult\s+castCameraToViewportRay\(const\s+float\s+nX,\s*const\s+float\s+nY,\s*float\s+maxDistance,\s*bool\s+ignorePlayer,\s*bool\s+ignoreActors\s*=\s*false,\s*bool\s+ignoreTerrain\s*=\s*false\);)(?=.*getIntersectionVisitor\(osgUtil::Intersector\*\s+intersector,\s*bool\s+ignorePlayer,\s*bool\s+ignoreActors,\s*bool\s+ignoreTerrain\s*=\s*false,\s*std::span<const\s+MWWorld::Ptr>\s+ignoreList\s*=\s*\{\}\);)(?=.*if\s*\(ignoreTerrain\)\s*mask\s*&=\s*~Mask_Terrain;)(?=.*mRootNode->accept\(\*getIntersectionVisitor\(intersector,\s*ignorePlayer,\s*ignoreActors,\s*false,\s*ignoreList\)\);)(?=.*const\s+bool\s+ignoreTerrain\s*=\s*true;)(?=.*castCameraToViewportRay\(x,\s*y,\s*maxDistance,\s*ignorePlayer,\s*ignoreActors,\s*ignoreTerrain\);)(?=.*castCameraToViewportRay\(\s*0\.5f,\s*0\.5f,\s*maxDistance,\s*ignorePlayer,\s*ignoreActors,\s*ignoreTerrain\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9143/#9142 keep HUD local map zoom configurable" -Text ($hud + "`n" + $mapSettings + "`n" + $settingsDefault) `
    -Pattern 'mLocalMapZoom\s*=\s*Settings::map\(\)\.mLocalMapHudZoom.*mLocalMapHudZoom.*makeClampSanitizerFloat\(0\.1f,\s*4\.f\).*local map hud zoom\s*=\s*0\.5' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9138 water ripple material avoids white flicker" -Text $ripples `
    -Pattern 'setAmbient\(osg::Material::FRONT_AND_BACK,\s*osg::Vec4f\(0\.f,\s*0\.f,\s*0\.f,\s*1\.f\)\).*setEmission\(osg::Material::FRONT_AND_BACK,\s*osg::Vec4f\(1\.f,\s*1\.f,\s*1\.f,\s*1\.f\)\).*setColorRange\(osgParticle::rangev4\(osg::Vec4f\(0,\s*0,\s*0,\s*0\.3f\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9003 filesystem VFS omits Unix hidden files and directories" -Text ($filesystemArchive + "`n" + $filesystemArchiveTest + "`n" + $componentsTestsCmake) `
    -Pattern '(?=.*bool\s+isUnixHidden\(const\s+std::filesystem::path&\s+path\).*filename\.front\(\)\s*==\s*''\.'';)(?=.*if\s*\(isUnixHidden\(entry\.path\(\)\)\).*entry\.is_directory\(\).*disable_recursion_pending\(\).*else\s+if\s*\(!entry\.is_directory\(\)\))(?=.*vfs/testfilesystemarchive\.cpp)(?=.*shouldOmitUnixHiddenFilesAndDirectories.*Music.*theme\.mp3.*\.root-hidden.*\.DS_Store.*\.hidden-album.*\.git.*EXPECT_TRUE\(archive\.contains\(Path::NormalizedView\("music/theme\.mp3"\)\)\).*EXPECT_FALSE\(archive\.contains\(Path::NormalizedView\("music/\.ds_store"\)\)\).*EXPECT_FALSE\(archive\.contains\(Path::NormalizedView\("\.git/config"\)\)\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9058 inaccessible Wine prefix is ignored during install path discovery" -Text ($wineUtils + "`n" + $linuxPath + "`n" + $wineUtilsTest + "`n" + $componentsTestsCmake) `
    -Pattern '(?=.*inline\s+bool\s+isRegularFile\(const\s+std::filesystem::path&\s+path\).*std::error_code\s+ec;.*std::filesystem::is_regular_file\(path,\s*ec\))(?=.*inline\s+bool\s+isDirectory\(const\s+std::filesystem::path&\s+path\).*std::error_code\s+ec;.*std::filesystem::is_directory\(path,\s*ec\))(?=.*if\s*\(!Impl::isRegularFile\(registryPath\)\)\s*return\s+paths;)(?=.*if\s*\(isDirectory\(installPath\)\)\s*return\s+installPath;)(?=.*std::filesystem::is_directory\(steam,\s*ec\))(?=.*files/wineutils\.cpp)(?=.*inaccessibleDefaultWinePrefixIsIgnored.*std::filesystem::permissions\(winePath,\s*std::filesystem::perms::none,\s*ec\).*EXPECT_TRUE\(getInstallPaths\(temp\.mPath\)\.empty\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9052 clamps shaded and full per-vertex sunlight before fragment shadow interpolation" -Text ($compatGroundcoverFrag + "`n" + $compatGroundcoverVert + "`n" + $compatObjectsFrag + "`n" + $compatObjectsVert + "`n" + $compatTerrainFrag + "`n" + $compatTerrainVert + "`n" + $vertexShaderCore + "`n" + $vertexShaderHeader) `
    -Pattern '(?=.*void\s+directionalLighting\(vec3\s+viewDir,\s*vec3\s+viewNormal,\s*float\s+shininess.*calcDirectionalLighting\(sun,\s*viewDir,\s*viewNormal,\s*shininess,\s*diffuseLight,\s*ambientLight,\s*specularLight\);)(?=.*void\s+pointLighting\(vec2\s+screenCoord,\s*vec3\s+viewDir,\s*vec3\s+viewPos,\s*vec3\s+viewNormal,\s*float\s+shininess)(?=.*shadedLighting\s*=\s*pointDiffuse\s*\+\s*pointAmbient\s*\+\s*sunAmbient;.*passLighting\s*=\s*shadedLighting\s*\+\s*sunDiffuse;.*clampLighting\(shadedLighting\);.*clampLighting\(passLighting\);)(?=.*shadedLighting\s*=\s*diffuseColor\s*\*\s*pointDiffuse\s*\+\s*ambientColor\s*\*\s*\(pointAmbient\s*\+\s*sunAmbient\)\s*\+\s*emissionColor\s*\*\s*emissiveMult;.*shadedSpecular\s*=\s*specularColor\s*\*\s*pointSpecular\s*\*\s*specStrength;.*passLighting\s*=\s*shadedLighting\s*\+\s*diffuseColor\s*\*\s*sunDiffuse;.*passSpecular\s*=\s*shadedSpecular\s*\+\s*specularColor\s*\*\s*sunSpecular\s*\*\s*specStrength;)(?=.*shadedLighting\s*=\s*diffuseColor\s*\*\s*pointDiffuse\s*\+\s*ambientColor\s*\*\s*\(pointAmbient\s*\+\s*sunAmbient\)\s*\+\s*emissionColor;.*passLighting\s*=\s*shadedLighting\s*\+\s*diffuseColor\s*\*\s*sunDiffuse;)(?=.*lighting\s*=\s*mix\(shadedLighting,\s*passLighting,\s*shadowing\);)(?=.*specular\s*=\s*mix\(shadedSpecular,\s*passSpecular,\s*shadowing\);)(?!.*shadowDiffuseLighting)(?!.*shadowSpecularLighting)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9108 AI filterPackages only removes packages on explicit false" -Text ($localScripts + "`n" + $aiLua) `
    -Pattern '(?=.*selfAPI\["_iterateAndFilterAiSequence"\]\s*=\s*\[\]\(SelfObject&\s+self,\s*sol::function\s+callback\).*sol::object\s+keep\s*=\s*LuaUtil::call\(callback,\s*entry\);.*return\s+keep\.is<bool>\(\)\s*&&\s*!keep\.as<bool>\(\);)(?=.*Iterate over all packages starting from the active one and remove those where `filterCallback` returns false\.)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9107 queued UI mode is visible to Lua immediately" -Text ($uiLua + "`n" + $luaApiPlayerTest + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*local\s+function\s+copyModeStack\(\).*local\s+function\s+replaceModeStack\(newStack\).*local\s+function\s+setMode\(mode,\s*options\).*local\s+oldStack\s*=\s*copyModeStack\(\).*replaceModeStack\(mode\s+and\s+\{mode\}\s+or\s+\{\}\).*ui\._setUiModeStack\(modeStack,\s*options\s+and\s+options\.target\).*replaceModeStack\(oldStack\))(?=.*queued UI mode is reflected immediately.*I\.UI\.setMode\(I\.UI\.MODE\.Interface\).*testing\.expectEqual\(I\.UI\.getMode\(\),\s*I\.UI\.MODE\.Interface.*testing\.expectEqual\(\s*I\.UI\.modes\[#I\.UI\.modes\],\s*I\.UI\.MODE\.Interface)(?=.*registerPlayerTest\(''queued UI mode is reflected immediately''\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9092 TogglePOV enables gamepad left-stick third-person zoom" -Text ($actionBindingsLua + "`n" + $controllerManager) `
    -Pattern '(?=.*input\.bindAction\(''Zoom3rdPerson''.*if\s+togglePOV\s+then.*local\s+leftStickY\s*=\s*input\.getAxisValue\(input\.CONTROLLER_AXIS\.LeftY\).*math\.abs\(leftStickY\)\s*<\s*0\.2.*controllerZoom\s*=\s*\(triggerRight\s*-\s*triggerLeft\s*-\s*leftStickY\)\s*\*\s*100\s*\*\s*dt)(?=.*mBindingsManager->actionIsActive\(A_TogglePOV\).*SDL_CONTROLLER_AXIS_TRIGGERRIGHT.*SDL_CONTROLLER_AXIS_TRIGGERLEFT.*SDL_CONTROLLER_AXIS_LEFTY.*Preview Mode Gamepad Zooming)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9091 script input binding captures controller buttons before GUI navigation" -Text ($inputSettingsLua + "`n" + $inputBindings + "`n" + $inputManagerBase + "`n" + $inputManagerImpHeader + "`n" + $inputManagerImp + "`n" + $controllerManagerHeader + "`n" + $controllerManager) `
    -Pattern '(?=.*local\s+function\s+setRecording\(value\).*input\._setCapturingControllerButtons\(recording\s*~=\s*nil\))(?=.*setRecording\s*\{.*id\s*=\s*id.*arg\s*=\s*arg.*refresh\s*=)(?=.*setRecording\(nil\))(?=.*api\["_setCapturingControllerButtons"\].*setCapturingControllerButtons\(v\))(?=.*virtual\s+void\s+setCapturingControllerButtons\(bool\s+enabled\)\s*=\s*0;)(?=.*void\s+setCapturingControllerButtons\(bool\s+enabled\)\s+override;)(?=.*setCapturingGuiControllerButtons\(enabled\))(?=.*void\s+setCapturingGuiControllerButtons\(bool\s+enabled\).*mCapturingGuiControllerButtons\s*=\s*enabled)(?=.*std::array<bool,\s*SDL_CONTROLLER_BUTTON_MAX>\s+mCapturedGuiControllerButtons)(?=.*captureGuiControllerButtonPress\(arg\)\s*\)\s*return;)(?=.*captureGuiControllerButtonRelease\(arg\)\s*\)\s*return;)(?=.*bool\s+ControllerManager::captureGuiControllerButtonPress\(const\s+SDL_ControllerButtonEvent&\s+arg\).*mCapturingGuiControllerButtons.*isGuiMode\(\).*mCapturedGuiControllerButtons\[arg\.button\]\s*=\s*true;.*return\s+true;)(?=.*bool\s+ControllerManager::captureGuiControllerButtonRelease\(const\s+SDL_ControllerButtonEvent&\s+arg\).*mCapturedGuiControllerButtons\[arg\.button\].*mCapturedGuiControllerButtons\[arg\.button\]\s*=\s*false;.*return\s+true;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9087 ModifyItemCondition supports tool maxCondition" -Text $mechanicsGlobalLua `
    -Pattern 'local\s+function\s+onModifyItemCondition\(data\).*local\s+item\s*=\s*data\.item.*local\s+itemData\s*=\s*Item\.itemData\(item\).*local\s+record\s*=\s*item\.type\.record\(item\).*local\s+maxCondition\s*=\s*record\.health\s+or\s+record\.maxCondition.*if\s+not\s+maxCondition\s+then\s*return\s*end.*itemData\.condition\s*=\s*math\.min\(maxCondition,\s*math\.max\(0,\s*itemData\.condition\s*\+\s*data\.amount\)\).*if\s+itemData\.condition\s*<=\s*0\s+and\s+record\.maxCondition\s+then\s*item:remove\(1\)\s*return\s*end.*data\.actor:sendEvent\(''Unequip'',\s*\{item\s*=\s*item\}\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9075 non-permanent Lua settings do not leak into permanent storage" -Text $settingsCommonLua `
    -Pattern '(?=.*local\s+valueSection\s*=\s*contextSection\(options\.key\).*local\s+argumentSection\s*=\s*contextSection\(options\.key\s*\.\.\s*argumentSectionPostfix\).*if\s+not\s+group\.permanentStorage\s+then\s*valueSection:setLifeTime\(storage\.LIFE_TIME\.Temporary\)\s*valueSection:reset\(\)\s*argumentSection:setLifeTime\(storage\.LIFE_TIME\.Temporary\)\s*argumentSection:reset\(\)\s*end)(?=.*for\s+i,\s*opt\s+in\s+ipairs\(options\.settings\)\s+do.*if\s+valueSection:get\(setting\.key\)\s*==\s*nil\s+then\s*valueSection:set\(setting\.key,\s*setting\.default\)\s*end)(?=.*onLoad\s*=\s*function\(saved\).*section:set\(key,\s*value\))(?=.*onSave\s*=\s*function\(\).*if\s+not\s+group\.permanentStorage\s+then\s*saved\[groupKey\]\s*=\s*contextSection\(groupKey\):asTable\(\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9137 playerLoaded does not silently re-enable toggled AI" -Text $mechanics `
    -Pattern 'void\s+MechanicsManager::playerLoaded\(\).*mUpdatePlayer\s*=\s*true;.*mClassSelected\s*=\s*true;.*mRaceSelected\s*=\s*true;(?!.*mAI\s*=\s*true;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9134 high-resolution trackpad wheel deltas are accumulated" -Text ($sdlInputWrapper + "`n" + $sdlInputWrapperHeader) `
    -Pattern 'mPreciseMouseWheelRemainder.*SDL_VERSION_ATLEAST\(2,\s*0,\s*18\).*evt\.wheel\.preciseY.*mPreciseMouseWheelRemainder\s*\+=\s*wheelY\s*\*\s*120\.f.*static_cast<Sint32>\(mPreciseMouseWheelRemainder\).*mPreciseMouseWheelRemainder\s*-=\s*static_cast<float>\(wheelDelta\)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8986 scaled mouse-look keeps precise relative motion" -Text ($sdlEvents + "`n" + $sdlInputWrapper + "`n" + $sdlInputWrapperHeader + "`n" + $mouseManager) `
    -Pattern '(?=.*float\s+xrelPrecise;)(?=.*float\s+yrelPrecise;)(?=.*float\s+mScaleX;)(?=.*float\s+mScaleY;)(?=.*static_cast<float>\(drawableSize\)\s*/\s*static_cast<float>\(windowSize\))(?=.*std::isfinite\(scale\)\s*&&\s*scale\s*>\s*0\.f)(?=.*mScaleX\s*=\s*getDrawableScale\(dw,\s*w\);)(?=.*mScaleY\s*=\s*getDrawableScale\(dh,\s*h\);)(?=.*packEvt\.x\s*=\s*mMouseX;.*packEvt\.y\s*=\s*mMouseY;)(?=.*packEvt\.xrelPrecise\s*=\s*static_cast<float>\(evt\.motion\.xrel\)\s*\*\s*mScaleX;)(?=.*packEvt\.yrelPrecise\s*=\s*static_cast<float>\(evt\.motion\.yrel\)\s*\*\s*mScaleY;)(?=.*packEvt\.xrel\s*=\s*roundMouseValue\(packEvt\.xrelPrecise\);)(?=.*packEvt\.yrel\s*=\s*roundMouseValue\(packEvt\.yrelPrecise\);)(?=.*packEvt\.xrelPrecise\s*=\s*packEvt\.yrelPrecise\s*=\s*0\.f;)(?=.*arg\.xrelPrecise\s*\*\s*cameraSensitivity)(?=.*arg\.yrelPrecise\s*\*\s*cameraSensitivity)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7458 Lua onKeyPress receives printable keys consumed by active text input" -Text $keyboardManager `
    -Pattern '(?=.*const\s+bool\s+consumedByPrintableTextInput\s*=\s*SDL_IsTextInputActive\(\)\s*&&\s*printableKey;)(?=.*bool\s+consumed\s*=\s*consumedByPrintableTextInput;)(?=.*if\s*\(!input->controlsDisabled\(\)\s*&&\s*!consumed\)\s*mBindingsManager->keyPressed\(arg\);)(?=.*if\s*\(!consumed\s*\|\|\s*consumedByPrintableTextInput\).*getLuaManager\(\)->inputEvent\(\s*\{\s*MWBase::LuaManager::InputEvent::KeyPressed,\s*arg\.keysym\s*\}\s*\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9105 SDL text input is released while window is unfocused" -Text ($sdlEvents + "`n" + $sdlInputWrapper + "`n" + $windowManagerImpHeader + "`n" + $windowManagerImp) `
    -Pattern '(?=.*virtual\s+void\s+windowFocusChange\(bool\s+focused\)\s*\{\})(?=.*SDL_WINDOWEVENT_FOCUS_GAINED.*mWindowHasFocus\s*=\s*true;.*updateMouseSettings\(\);.*mWindowListener->windowFocusChange\(true\))(?=.*SDL_WINDOWEVENT_FOCUS_LOST.*mWindowHasFocus\s*=\s*false;.*updateMouseSettings\(\);.*mWindowListener->windowFocusChange\(false\))(?=.*void\s+windowFocusChange\(bool\s+focused\)\s+override;)(?=.*void\s+WindowManager::windowFocusChange\(bool\s+focused\).*if\s*\(focused\).*onKeyFocusChanged\(MyGUI::InputManager::getInstance\(\)\.getKeyFocusWidget\(\)\).*else\s+if\s*\(SDL_IsTextInputActive\(\)\s*==\s*SDL_TRUE\).*SDL_StopTextInput\(\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9140 camera Lua API supports additive identified offsets" -Text ($cmakeLists + "`n" + $cameraBindings + "`n" + $cameraRender + "`n" + $cameraRenderHeader + "`n" + $cameraLua) `
    -Pattern 'OPENMW_LUA_API_REVISION\s+143.*setAdditiveExtraPitch.*setAdditiveExtraYaw.*setAdditiveExtraRoll.*setAdditiveFirstPersonOffset.*mExtraPitchBase\s*=\s*angle\s*-\s*getAdditiveAngleSum\(mAdditiveExtraPitch\).*mFirstPersonOffsetBase\s*=\s*v\s*-\s*getAdditiveFirstPersonOffsetSum\(\).*mAdditiveExtraPitch.*mAdditiveExtraYaw.*mAdditiveExtraRoll.*mAdditiveFirstPersonOffset.*@function \[parent=#camera\] setAdditiveExtraPitch.*@function \[parent=#camera\] setAdditiveFirstPersonOffset' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9130 ingredient templates clear replaced effect skill and attribute metadata" -Text ($ingredientBindings + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*void\s+setFromTable\(ESM::Ingredient::IRDTstruct&\s+data,\s*uint32_t\s+i,\s*const\s+sol::lua_table&\s+table\).*blankIndex\(data,\s*i\);.*addPropertyFromTable\(table,\s*"id",\s*data\.mEffectID\[i\]\);.*addPropertyFromTable\(table,\s*"affectedAttribute",\s*data\.mAttributes\[i\]\);.*addPropertyFromTable\(table,\s*"affectedSkill",\s*data\.mSkills\[i\]\);)(?=.*local\s+skillTemplateIngredient\s*=\s*types\.Ingredient\.createRecordDraft\(\{.*id\s*=\s*"fortify skill",\s*affectedSkill\s*=\s*"alchemy".*local\s+patchedIngredient\s*=\s*types\.Ingredient\.createRecordDraft\(\{.*template\s*=\s*skillTemplateIngredient.*id\s*=\s*"restore fatigue".*testing\.expectEqual\(patchedIngredient\.effects\[1\]\.affectedAttribute,\s*""\).*testing\.expectEqual\(patchedIngredient\.effects\[1\]\.affectedSkill,\s*""\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9133 DialogueResponse exposes selected DialogueRecordInfo directly" -Text ($cmakeLists + "`n" + $openmwCmakeLists + "`n" + $luaManagerImp + "`n" + $dialogueBindings + "`n" + $dialogueInfo + "`n" + $dialogueInfoHeader + "`n" + $userdataSerializer + "`n" + $coreLua) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*add_openmw_dir\s*\(mwlua.*dialoguebindings\s+dialogueinfo)(?=.*class\s+DialogueInfo.*DialogueInfo\(const\s+ESM::Dialogue&\s+record,\s*const\s+ESM::DialInfo&\s+info\).*requireInfo\(\)\s+const)(?=.*data\["info"\]\s*=\s*DialogueInfo\(record,\s*info\);)(?=.*sDialogueInfoTypeName\s*=\s*"dialinfo")(?=.*data\.is<DialogueInfo>\(\))(?=.*appendDialogueInfo\(out,\s*data\.as<DialogueInfo>\(\)\))(?=.*typeName\s*==\s*sDialogueInfoTypeName)(?=.*sol::stack::push<DialogueInfo>)(?=.*new_usertype<MWLua::DialogueInfo>\("ESM3_Dialogue_Response_Info"\))(?=.*@type DialogueResponseEvent.*@field #DialogueRecordInfo info)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9121 Lua AI combat accepts only actor targets and purges invalid combat packages" -Text ($aiSequence + "`n" + $aiSequenceHeader + "`n" + $aiCombat + "`n" + $localScripts + "`n" + $aiLua) `
    -Pattern '(?=.*void\s+removeInvalidCombatPackages\(\);)(?=.*hasNonActorCombatTarget)(?=.*target\.isEmpty\(\)\s*\|\|\s*!target\.getClass\(\)\.isActor\(\))(?=.*if\s*\(hasNonActorCombatTarget\(\*package\)\)\s*continue;)(?=.*AI Combat target must be an actor)(?=.*types\.Actor\.objectIsInstance\(args\.target\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9097 paralyzed actors do not emit dialogue battlecries" -Text ($dialogueManager + "`n" + $aiCombat + "`n" + $mechanics) `
    -Pattern '(?=.*bool\s+DialogueManager::say\(const\s+MWWorld::Ptr&\s+actor,\s*const\s+ESM::RefId&\s+topic\).*const\s+MWMechanics::CreatureStats&\s+creatureStats\s*=\s*actor\.getClass\(\)\.getCreatureStats\(actor\);.*creatureStats\.getKnockedDown\(\)\s*\|\|\s*creatureStats\.isParalyzed\(\).*return\s+false;)(?=.*DialogueManager\(\)->say\(actor,\s*ESM::RefId::stringRefId\("attack"\)\))(?=.*DialogueManager\(\)->say\(ptr,\s*ESM::RefId::stringRefId\("attack"\)\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9096 exposes failed skill use events to Lua" -Text ($luaManagerBase + "`n" + $luaManagerImpHeader + "`n" + $luaManagerImp + "`n" + $engineEventsHeader + "`n" + $engineEvents + "`n" + $localScriptsHeader + "`n" + $worldClass + "`n" + $worldClassHeader + "`n" + $npcClass + "`n" + $npcClassHeader + "`n" + $combatMechanics + "`n" + $spellCasting + "`n" + $securityMechanics + "`n" + $pickpocketModel + "`n" + $skillHandlersLua + "`n" + $luaApiPlayerTest) `
    -Pattern '(?=.*virtual\s+void\s+skillUseFailed\(const\s+MWWorld::Ptr&\s+actor,\s*ESM::RefId\s+skillId,\s*int\s+useType,\s*float\s+scale\)\s*=\s*0;)(?=.*void\s+skillUseFailed\(const\s+MWWorld::Ptr&\s+actor,\s*ESM::RefId\s+skillId,\s*int\s+useType,\s*float\s+scale\)\s+override;)(?=.*void\s+LuaManager::skillUseFailed\(const\s+MWWorld::Ptr&\s+actor,\s*ESM::RefId\s+skillId,\s*int\s+useType,\s*float\s+scale\).*EngineEvents::OnSkillUseFailed\{\s*getId\(actor\),\s*skillId\.serializeText\(\),\s*useType,\s*scale\s*\})(?=.*struct\s+OnSkillUseFailed\s*\{.*ESM::RefNum\s+mActor;.*std::string\s+mSkill;.*int\s+useType;.*float\s+scale;)(?=.*using\s+Event\s*=\s*std::variant<.*OnSkillUse,\s*OnSkillUseFailed,\s*OnSkillLevelUp)(?=.*void\s+operator\(\)\(const\s+OnSkillUseFailed&\s+event\)\s+const.*scripts->onSkillUseFailed\(event\.mSkill,\s*event\.useType,\s*event\.scale\);)(?=.*void\s+onSkillUseFailed\(std::string_view\s+skillId,\s*int\s+useType,\s*float\s+scale\).*callEngineHandlers\(mOnSkillUseFailed,\s*skillId,\s*useType,\s*scale\);)(?=.*EngineHandlerList\s+mOnSkillUseFailed\{\s*"_onSkillUseFailed"\s*\};)(?=.*virtual\s+void\s+skillUsageFailed\(.*ESM::RefId\s+skill,\s*int\s+usageType,\s*float\s+extraFactor\s*=\s*1\.f\)\s+const;)(?=.*void\s+Class::skillUsageFailed\(.*throw\s+std::runtime_error\("class does not represent an actor"\);)(?=.*void\s+Npc::skillUsageFailed\(.*getLuaManager\(\)->skillUseFailed\(ptr,\s*skill,\s*usageType,\s*extraFactor\);)(?=.*skillUsageFailed\(ptr,\s*weapskill,\s*ESM::Skill::Weapon_SuccessfulHit\);)(?=.*skillUsageFailed\(attacker,\s*weaponSkill,\s*ESM::Skill::Weapon_SuccessfulHit\);)(?=.*skillUsageFailed\(mCaster,\s*school,\s*ESM::Skill::Spellcast_Success\);)(?=.*skillUsageFailed\(mActor,\s*ESM::Skill::Security,\s*ESM::Skill::Security_PickLock\);)(?=.*skillUsageFailed\(mActor,\s*ESM::Skill::Security,\s*ESM::Skill::Security_DisarmTrap\);)(?=(?:.*skillUsageFailed\(player,\s*ESM::Skill::Sneak,\s*ESM::Skill::Sneak_PickPocket\);){2})(?=.*local\s+skillUsedFailedHandlers\s*=\s*\{\})(?=.*version\s*=\s*3)(?=.*addSkillUsedFailedHandler\s*=\s*function\(handler\).*skillUsedFailedHandlers\[#skillUsedFailedHandlers\s*\+\s*1\]\s*=\s*handler)(?=.*skillUsedFailed\s*=\s*skillUsedFailed)(?=.*_onSkillUseFailed\s*=\s*function\s*\(skillid,\s*useType,\s*scale\).*I\.SkillProgression\.skillUsedFailed\(skillid,\s*\{\s*useType\s*=\s*useType,\s*scale\s*=\s*scale\s*\}\))(?=.*registerLocalTest\(''skill use failure handlers are exposed''.*I\.SkillProgression\.version,\s*3.*addSkillUsedFailedHandler.*skillUsedFailed.*Skill\.record\(''sneak''\)\.skillGain\[useType\s*\+\s*1\])' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9132 Lua replacement hides an already-open Training window" -Text ($windowBaseHeader + "`n" + $trainingWindowHeader + "`n" + $trainingWindow + "`n" + $uiLua) `
    -Pattern '(?=.*virtual\s+void\s+setDisabledByLua\(bool\s+disabled\).*mDisabledByLua\s*=\s*disabled;.*if\s*\(disabled\)\s*setVisible\(false\);)(?=.*void\s+setDisabledByLua\(bool\s+disabled\)\s+override;)(?=.*void\s+TrainingWindow::setDisabledByLua\(bool\s+disabled\).*WindowBase::setDisabledByLua\(disabled\);.*mProgressBar\.setDisabledByLua\(disabled\);)(?=.*registerWindow\(window,\s*showFn,\s*hideFn\).*ui\._setWindowDisabled\(window,\s*true\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9118 dialogue goodbye uses Interface localization instead of raw sGoodbye" -Text ($dialogueWindow + "`n" + $dialogueLayout) `
    -Pattern '(?=.*std::string\s+getGoodbyeText\(\).*getL10nManager\(\)->getMessage\("Interface",\s*"Goodbye"\).*replaceAll\(goodbye,\s*"\\xE2\\x80\\xAF",\s*" "\).*replaceAll\(goodbye,\s*"___",\s*" "\))(?=.*mGoodbyeButton->setCaption\(MyGUI::UString\(goodbye\)\))(?=.*mControllerButtons\.mB\s*=\s*goodbye;)(?=.*const\s+std::string\s+goodbye\s*=\s*getGoodbyeText\(\);.*typesetter->write\(questionStyle,\s*goodbye\))(?=.*<Property\s+key="Caption"\s+value="#\{Interface:Goodbye\}"\/>)(?!.*<Property\s+key="Caption"\s+value="#\{sGoodbye\}"\/>)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9076 dialogue controller focus stays centered in topic list" -Text ($dialogueWindow + "`n" + $widgetsListHeader + "`n" + $widgetsList) `
    -Pattern '(?=.*void\s+centerItem\(std::string_view\s+name\);)(?=.*void\s+scrollToBottom\(\);)(?=.*void\s+MWList::centerItem\(std::string_view\s+name\).*getItemWidget\(name\).*getHeight\(\).*std::clamp.*setViewOffset\(MyGUI::IntPoint\(0,\s*-centered\)\))(?=.*void\s+MWList::scrollToBottom\(\).*std::max\(0,\s*mScrollView->getCanvasSize\(\)\.height\s*-\s*mScrollView->getHeight\(\)\).*setViewOffset\(MyGUI::IntPoint\(0,\s*-bottom\)\))(?=.*void\s+DialogueWindow::setControllerFocus\(size_t\s+index,\s*bool\s+focused\).*const\s+size_t\s+itemCount\s*=\s*mTopicsList->getItemCount\(\);.*index\s*==\s*itemCount.*mTopicsList->scrollToBottom\(\).*const\s+std::string&\s+keyword\s*=\s*mTopicsList->getItemNameAt\(index\).*mTopicsList->centerItem\(keyword\))(?!.*for\s*\(int\s+i\s*=\s*6;\s*i\s*<\s*static_cast<int>\(index\);\s*i\+\+\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9005 dialogue controller topic selector is visually distinct from important topics" -Text ($dialogueWindow + "`n" + $dialogueLayout + "`n" + $listSkin) `
    -Pattern '(?=.*ResourceSkin"\s+name="MW_DialogueTopicsList".*ListItemSkin"\s+value="MW_DialogueTopicLine")(?=.*ResourceSkin"\s+name="MW_DialogueTopicLine".*normal_checked"\s+colour="#\{fontcolour=header\}".*highlighted_checked"\s+colour="#\{fontcolour=header\}".*pushed_checked"\s+colour="#\{fontcolour=header\}")(?=.*ResourceSkin"\s+name="MW_DialogueTopicLine_Specific".*color topic specific.*normal_checked"\s+colour="#\{fontcolour=header\}".*highlighted_checked"\s+colour="#\{fontcolour=header\}".*pushed_checked"\s+colour="#\{fontcolour=header\}")(?=.*ResourceSkin"\s+name="MW_DialogueTopicLine_Exhausted".*color topic exhausted.*normal_checked"\s+colour="#\{fontcolour=header\}".*highlighted_checked"\s+colour="#\{fontcolour=header\}".*pushed_checked"\s+colour="#\{fontcolour=header\}")(?=.*<Widget\s+type="MWList"\s+skin="MW_DialogueTopicsList"[^>]*name="TopicsList")(?=.*changeWidgetSkin\("MW_DialogueTopicLine_Specific"\))(?=.*changeWidgetSkin\("MW_DialogueTopicLine_Exhausted"\))(?!.*<Widget\s+type="MWList"\s+skin="MW_SimpleList"[^>]*name="TopicsList")(?!.*changeWidgetSkin\("MW_ListLine_Specific"\))(?!.*changeWidgetSkin\("MW_ListLine_Exhausted"\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9113 exposes MWScript resurrect behavior to Lua" -Text ($cmakeLists + "`n" + $openmwCmakeLists + "`n" + $actorBindings + "`n" + $statsExtensions + "`n" + $resurrectMechanics + "`n" + $resurrectMechanicsHeader + "`n" + $typesLua) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*add_openmw_dir\s*\(mwmechanics.*spelleffects\s+resurrect)(?=.*#include\s+"apps/openmw/mwmechanics/resurrect\.hpp".*actor\["resurrect"\]\s*=\s*\[context\]\(const\s+Object&\s+object\).*Can only be used in global scripts or in local scripts on self\..*context\.mLuaManager->addAction\(.*MWMechanics::resurrect\(obj\.ptr\(\)\);.*"ResurrectAction"\);)(?=.*#include\s+"\.\./mwmechanics/resurrect\.hpp".*class\s+OpResurrect.*MWMechanics::resurrect\(ptr\);)(?=.*void\s+resurrect\(const\s+MWWorld::Ptr&\s+ptr\);)(?=.*void\s+resurrect\(const\s+MWWorld::Ptr&\s+ptr\).*if\s*\(!ptr\.getClass\(\)\.isActor\(\)\).*ptr\s*==\s*MWMechanics::getPlayer\(\).*getMechanicsManager\(\)->resurrect\(ptr\).*getStateManager\(\)->resumeGame\(\).*getCreatureStats\(ptr\)\.isDead\(\).*undeleteObject\(ptr\).*removeContainerScripts\(ptr\).*setCustomData\(nullptr\))(?=.*@function \[parent=#Actor\] resurrect)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9071 exposes actor inertial force to Lua" -Text ($cmakeLists + "`n" + $worldBase + "`n" + $worldImpHeader + "`n" + $worldImp + "`n" + $physicsSystemHeader + "`n" + $physicsSystem + "`n" + $actorBindings + "`n" + $typesLua) `
    -Pattern '(?=.*OPENMW_LUA_API_REVISION\s+143)(?=.*virtual\s+osg::Vec3f\s+getActorInertialForce\(const\s+MWWorld::ConstPtr&\s+actor\)\s+const\s*=\s*0;)(?=.*virtual\s+void\s+setActorInertialForce\(const\s+MWWorld::Ptr&\s+actor,\s*const\s+osg::Vec3f&\s+force\)\s*=\s*0;)(?=.*osg::Vec3f\s+PhysicsSystem::getInertialForce\(const\s+MWWorld::ConstPtr&\s+actor\)\s+const.*getActor\(actor\).*getInertialForce\(\))(?=.*void\s+PhysicsSystem::setInertialForce\(const\s+MWWorld::Ptr&\s+actor,\s*const\s+osg::Vec3f&\s+force\).*getActor\(actor\).*setInertialForce\(force\))(?=.*actor\["getInertialForce"\].*getActorInertialForce\(ptr\))(?=.*actor\["setInertialForce"\].*Misc::FiniteVec3f.*Can only be used in global scripts or in local scripts on self\..*setActorInertialForce\(ptr,\s*force\))(?=.*@function \[parent=#Actor\] getInertialForce)(?=.*@function \[parent=#Actor\] setInertialForce)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8631 Fall script instruction forces actors into falling physics" -Text ($compilerExtensions + "`n" + $miscExtensions + "`n" + $worldBase + "`n" + $worldImpHeader + "`n" + $worldImp + "`n" + $physicsActorHeader + "`n" + $physicsSystemHeader + "`n" + $physicsSystem + "`n" + $physicsActor + "`n" + $movementSolver) `
    -Pattern '(?=.*registerInstruction\("fall",\s*"",\s*opcodeFall,\s*opcodeFallExplicit\))(?=.*class\s+OpFall.*forceActorFall\(ptr\))(?=.*virtual\s+void\s+forceActorFall\(const\s+MWWorld::Ptr&\s+actor\)\s*=\s*0;)(?=.*void\s+World::forceActorFall\(const\s+Ptr&\s+actor\).*mPhysics->forceActorFall\(actor\);)(?=.*bool\s+isForceFalling\(\)\s+const\s*\{\s*return\s+mForceFalling;\s*\})(?=.*void\s+PhysicsSystem::forceActorFall\(const\s+MWWorld::Ptr&\s+actor\).*force\.z\(\)\s*=\s*std::min.*physactor->forceFall\(\);)(?=.*const\s+bool\s+forceFalling\s*=\s*physicActor->isForceFalling\(\);.*!forceFalling.*WaterWalking)(?=.*const\s+bool\s+flying\s*=\s*actor\.mFlying\s*&&\s*!actor\.mForceFalling;)(?=.*actor\.mForceFalling\s*\?\s*-std::numeric_limits<float>::max\(\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #6934 combat packages expire after fClearCorpseDelay game hours" -Text ($aiCombat + "`n" + $aiCombatHeader + "`n" + $esmAiSequence + "`n" + $esmAiSequenceHeader) `
    -Pattern '(?=.*struct\s+AiCombat\s*:\s*AiPackage.*ESM::TimeStamp\s+mStartTime;.*bool\s+mHasStartTime\s*=\s*false;)(?=.*void\s+AiCombat::load\(ESMReader&\s+esm\).*loadActorId\(esm,\s*"TARG",\s*mTargetActor\);.*mHasStartTime\s*=\s*esm\.isNextSub\("STAR"\);.*mStartTime\.load\(esm,\s*"STAR"\);)(?=.*void\s+AiCombat::save\(ESMWriter&\s+esm\).*writeFormId\(mTargetActor,\s*true,\s*"TARG"\);.*writeHNT\("STAR",\s*mStartTime\);)(?=.*AiCombat::AiCombat\(const\s+MWWorld::Ptr&\s+actor\).*mStartTime\(MWBase::Environment::get\(\)\.getWorld\(\)->getTimeStamp\(\)\))(?=.*AiCombat::AiCombat\(const\s+ESM::AiSequence::AiCombat\*\s+combat\).*combat->mHasStartTime\s*\?\s*MWWorld::TimeStamp\(combat->mStartTime\).*getTimeStamp\(\))(?=.*bool\s+AiCombat::execute.*if\s*\(shouldExpireCombat\(\)\)\s*return\s+true;)(?=.*bool\s+AiCombat::shouldExpireCombat\(\)\s+const.*search\("fClearCorpseDelay"\).*if\s*\(clearCorpseDelay\s*==\s*nullptr\)\s*return\s+false;.*getTimeStamp\(\)\s*-\s*mStartTime.*elapsedHours\s*>=\s*clearCorpseDelayHours;)(?=.*combat->mStartTime\s*=\s*mStartTime\.toEsm\(\);.*combat->mHasStartTime\s*=\s*true;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #3602 implements NiBillboardNode billboard modes" -Text ($nifNodeHeader + "`n" + $nifNode + "`n" + $nifLoader) `
    -Pattern '(?=.*struct\s+NiBillboardNode\s*:\s*NiNode\s*\{\s*int\s+mMode;)(?=.*void\s+NiBillboardNode::read\(NIFStream\*\s+nif\).*mMode\s*=\s*nif->get<uint16_t>\(\)\s*&\s*0x7.*mMode\s*=\s*\(mFlags\s*>>\s*5\)\s*&\s*0x3)(?=.*class\s+BillboardCallback.*enum\s+Mode\s*\{\s*AlwaysFaceCamera\s*=\s*0,\s*RotateAboutUp\s*=\s*1,\s*RigidFaceCamera\s*=\s*2,\s*AlwaysFaceCenter\s*=\s*3,\s*RigidFaceCenter\s*=\s*4)(?=.*explicit\s+BillboardCallback\(int\s+mode\).*mMode\(mode\))(?=.*case\s+RotateAboutUp:\s*rotateAboutUp\(modelView\);)(?=.*case\s+RigidFaceCamera:\s*rigidFaceCamera\(modelView\);)(?=.*case\s+AlwaysFaceCenter:\s*alwaysFaceCamera\(modelView,\s*directionToCamera\(modelView\)\);)(?=.*case\s+RigidFaceCenter:\s*rigidFaceCenter\(modelView\);)(?=.*alwaysFaceCamera\(modelView,\s*osg::Vec3d\(0,\s*0,\s*1\)\);)(?=.*static\s+void\s+rotateAxes\(osg::Matrix&\s+modelView,\s*const\s+osg::Quat&\s+rotation\).*rotation\s*\*\s*value)(?=.*static\s+void\s+rotateAboutUp\(osg::Matrix&\s+modelView\).*normal\s*-=\s*up\s*\*\s*\(normal\s*\*\s*up\).*std::atan2\(sinAngle,\s*cosAngle\))(?=.*static\s+void\s+rigidFaceCenter\(osg::Matrix&\s+modelView\).*directionToCamera\(modelView\).*setRigidAxes\(modelView,\s*x,\s*y,\s*z\);)(?=.*static_cast<const\s+Nif::NiBillboardNode\*>\(nifNode\).*new\s+BillboardCallback\(billboardNode->mMode\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #3559 magic VFX scaling keeps particle billboard size authored" -Text ($mwrenderUtilHeader + "`n" + $mwrenderUtil + "`n" + $mwrenderEffectManager + "`n" + $mwrenderAnimation) `
    -Pattern '(?=.*void\s+useWorldspaceParticleSize\(osg::Node&\s+node\);)(?=.*#include\s+<osgParticle/ParticleSystem>)(?=.*struct\s+ParticleScaleReferenceFrameVisitor\s*:\s*osg::NodeVisitor.*osg::NodeVisitor\(TRAVERSE_ALL_CHILDREN\).*dynamic_cast<osgParticle::ParticleSystem\*>\(&drw\).*setParticleScaleReferenceFrame\(osgParticle::ParticleSystem::WORLD_COORDINATES\))(?=.*void\s+useWorldspaceParticleSize\(osg::Node&\s+node\).*ParticleScaleReferenceFrameVisitor\s+visitor;.*node\.accept\(visitor\);)(?=.*void\s+EffectManager::addEffect.*if\s*\(isMagicVFX\).*overrideFirstRootTexture\(VFS::Path::toNormalized\(textureOverride\),\s*mResourceSystem,\s*\*node\);.*useWorldspaceParticleSize\(\*node\);)(?=.*void\s+Animation::addEffect.*overrideFirstRootTexture\(VFS::Path::toNormalized\(texture\),\s*mResourceSystem,\s*\*node\);.*useWorldspaceParticleSize\(\*node\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9036 NIF particle systems keep authored bounds" -Text ($nifOsgParticleHeader + "`n" + $nifOsgParticle + "`n" + $componentsTestsCmake + "`n" + $nifOsgParticleTest) `
    -Pattern '(?=.*osg::BoundingBox\s+computeBoundingBox\(\)\s+const\s+override;)(?=.*osg::BoundingBox\s+ParticleSystem::computeBoundingBox\(\)\s+const.*const\s+osg::BoundingBox&\s+initialBound\s*=\s*getInitialBound\(\);.*if\s*\(initialBound\.valid\(\)\).*return\s+initialBound;.*return\s+osgParticle::ParticleSystem::computeBoundingBox\(\);)(?=.*nifosg/testparticle\.cpp)(?=.*authoredInitialBoundOverridesDynamicParticleBounds.*setInitialBound\(authoredBound\).*setPosition\(osg::Vec3f\(10000\.f,\s*10000\.f,\s*10000\.f\)\).*computeBoundingBox\(\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8982 static NIF geometry uses authored bounding spheres" -Text ($nifLoader + "`n" + $nifOsgLoaderTest + "`n" + $sceneUtilOptimizer) `
    -Pattern '(?=.*class\s+AuthoredBoundingSphereCallback\s*:\s*public\s+osg::Node::ComputeBoundingSphereCallback)(?=.*bool\s+isUsableAuthoredBound\(const\s+osg::BoundingSpheref&\s+bound\).*bound\.valid\(\).*std::isfinite\(bound\.radius\(\)\).*isFinite\(bound\.center\(\)\))(?=.*bool\s+enclosesVertices\(const\s+osg::BoundingSpheref&\s+bound,\s*const\s+std::vector<osg::Vec3f>&\s+vertices\).*toleratedRadiusSquared.*length2\(\)\s*>\s*toleratedRadiusSquared)(?=.*bool\s+shouldUseAuthoredGeometryBound\(.*hasAnimatedParents.*loadedGeometry.*isAnimated.*!loadedGeometry\s*\|\|\s*isAnimated\s*\|\|\s*hasAnimatedParents\s*\|\|\s*!nifNode->mController\.empty\(\).*isTypeNiGeometry\(nifNode->mRecordType\).*!geometry->mSkin\.empty\(\).*isUsableAuthoredBound\(data->mBoundingSphere\).*enclosesVertices\(data->mBoundingSphere,\s*data->mVertices\))(?=.*void\s+applyAuthoredGeometryBound\(.*SceneUtil::transformBoundingSphere\(nifNode->mTransform\.toMatrix\(\),\s*bound\).*setComputeBoundingSphereCallback\(.*AuthoredBoundingSphereCallback)(?=.*loadedGeometry\s*=\s*node->getNumChildren\(\)\s*>\s*childCount;)(?=.*shouldUseAuthoredGeometryBound\(nifNode,\s*hasAnimatedParents,\s*loadedGeometry,\s*isAnimated\).*applyAuthoredGeometryBound\(nifNode,\s*\*node\))(?=.*setComputeBoundingSphereCallback\(transform->getComputeBoundingSphereCallback\(\)\))(?=.*!node\.getComputeBoundingSphereCallback\(\))(?=.*staticNiGeometryShouldUseAuthoredBoundingSphere)(?=.*optimizerShouldPreserveAuthoredBoundingSphere)(?=.*staticNiGeometryShouldRejectAuthoredBoundingSphereThatDoesNotEncloseVertices)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8390 non-finite rotations are sanitized before physics" -Text ($miscConvert + "`n" + $worldImp + "`n" + $worldScene + "`n" + $componentsTestsCmake + "`n" + $miscConvertTest) `
    -Pattern '(?=.*float\s+makeFiniteRotationAngle\(float\s+value\).*std::isfinite\(value\)\s*\?\s*value\s*:\s*0\.f)(?=.*osg::Quat\s+makeOsgQuat\(const\s+float\s+\(&rotation\)\[3\]\).*makeFiniteRotationAngle\(rotation\[0\]\).*makeFiniteRotationAngle\(rotation\[1\]\).*makeFiniteRotationAngle\(rotation\[2\]\))(?=.*btQuaternion\s+makeBulletQuaternion\(const\s+float\s+\(&rotation\)\[3\]\).*makeFiniteRotationAngle\(rotation\[0\]\).*makeFiniteRotationAngle\(rotation\[1\]\).*makeFiniteRotationAngle\(rotation\[2\]\))(?=.*objRot\[0\]\s*=\s*Misc::Convert::makeFiniteRotationAngle\(objRot\[0\]\);.*objRot\[1\]\s*=\s*Misc::Convert::makeFiniteRotationAngle\(objRot\[1\]\);.*objRot\[2\]\s*=\s*Misc::Convert::makeFiniteRotationAngle\(objRot\[2\]\);)(?=.*makeActorOsgQuat.*makeFiniteRotationAngle\(position\.rot\[2\]\))(?=.*makeInversedOrderObjectOsgQuat.*makeFiniteRotationAngle\(position\.rot\[0\]\).*makeFiniteRotationAngle\(position\.rot\[1\]\).*makeFiniteRotationAngle\(position\.rot\[2\]\))(?=.*misc/testconvert\.cpp)(?=.*nonFiniteEulerAnglesBecomeZeroAngleRotations.*quiet_NaN.*infinity.*makeOsgQuat\(rotation\).*makeBulletQuaternion\(rotation\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8969 non-finite object scales fall back to default scale" -Text ($esmCellRef + "`n" + $mwCellRef + "`n" + $esm3SaveLoadTest) `
    -Pattern '(?=.*float\s+sanitizeScale\(float\s+scale\).*std::isfinite\(scale\).*std::clamp\(scale,\s*MinScale,\s*MaxScale\))(?=.*case\s+fourCC\("XSCL"\).*cellRef\.mScale\s*=\s*sanitizeScale\(cellRef\.mScale\);)(?=.*const\s+float\s+scale\s*=\s*sanitizeScale\(mScale\);.*if\s*\(scale\s*!=\s*DefaultScale\).*writeHNT\("XSCL",\s*scale\);)(?=.*void\s+CellRef::setScale\(float\s+scale\).*scale\s*=\s*sanitizeScale\(scale\);)(?=.*cellRefShouldSanitizeNonFiniteScaleOnLoad.*quiet_NaN\(\).*EXPECT_EQ\(result\.mScale,\s*1\.f\);)(?=.*cellRefShouldNotSaveNonFiniteScale.*quiet_NaN\(\).*Not\(HasSubstr\("XSCL"\)\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #3366 NiSortAdjustNode sort state stays scoped to its subtree" -Text $nifLoader `
    -Pattern '(?=.*const\s+Nif::NiSortAdjustNode\*\s+const\s+previousPushedSorter\s*=\s*mPushedSorter;)(?=.*const\s+Nif::NiSortAdjustNode\*\s+const\s+previousLastAppliedNoInheritSorter\s*=\s*mLastAppliedNoInheritSorter;)(?=.*if\s*\(nifNode->mRecordType\s*==\s*Nif::RC_NiSortAdjustNode\).*mLastAppliedNoInheritSorter\s*=\s*mPushedSorter;.*mPushedSorter\s*=\s*sortNode;)(?=.*if\s*\(nifNode->mRecordType\s*==\s*Nif::RC_NiFltAnimationNode\).*activateSequenceNode\(currentNode,\s*nifNode\);.*mPushedSorter\s*=\s*previousPushedSorter;.*mLastAppliedNoInheritSorter\s*=\s*previousLastAppliedNoInheritSorter;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #3803 stationary grounded NPCs keep mod-authored seated placement" -Text $movementSolver `
    -Pattern '(?=.*preserveStationaryGroundedActor\s*=\s*!actor\.mIsPlayer\s*&&\s*(?:!actor\.mFlying|!flying)\s*&&\s*!actor\.mWaterCollision\s*&&\s*actor\.mIsOnGround\s*&&\s*!actor\.mIsOnSlope\s*&&\s*actor\.mMovement\.length2\(\)\s*==\s*0\.f\s*&&\s*actor\.mInertia\.length2\(\)\s*==\s*0\.f\s*&&\s*!forceGroundTest\s*&&\s*newPosition\.z\(\)\s*>=\s*swimlevel)(?=.*if\s*\(preserveStationaryGroundedActor\).*actor\.mIsOnGround\s*=\s*true;.*actor\.mIsOnSlope\s*=\s*false;.*actor\.mInertia\s*=\s*osg::Vec3f\(\);.*actor\.mPosition\.z\(\)\s*-=\s*actor\.mHalfExtentsZ;.*return;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7570 SetPos hard-syncs actor physics after scripted repositioning" -Text ($transformationExtensions + "`n" + $physicsSystem + "`n" + $physicsActor) `
    -Pattern '(?=.*class\s+OpSetPos\s*:\s*public\s+Interpreter::Opcode0)(?=.*MWBase::World\*\s+world\s*=\s*MWBase::Environment::get\(\)\.getWorld\(\);)(?=.*MWWorld::Ptr\s+newPtr\s*=\s*ptr\.getClass\(\)\.isActor\(\)\s*\?\s*world->moveObject\(ptr,\s*newPos,\s*true,\s*true\)\s*:\s*world->moveObjectBy\(ptr,\s*newPos\s*-\s*curPos,\s*true\);)(?=.*updatePtr\(ptr,\s*newPtr\))(?=.*void\s+PhysicsSystem::updatePosition\(const\s+MWWorld::Ptr&\s+ptr\).*foundActor->second->updatePosition\(\);.*mTaskScheduler->updateSingleAabb\(foundActor->second,\s*true\);)(?=.*void\s+Actor::updatePosition\(\).*mPosition\s*=\s*worldPosition;.*mSimulationPosition\s*=\s*worldPosition;.*mPositionOffset\s*=\s*osg::Vec3f\(\);.*mSkipSimulation\s*=\s*true;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #806 actor sweeps ignore authored back faces on world triangle meshes" -Text $actorConvexCallback `
    -Pattern '(?=.*#include\s+<BulletCollision/NarrowPhaseCollision/btRaycastCallback\.h>)(?=.*class\s+FrontFaceRayCallback\s*:\s*public\s+btCollisionWorld::ClosestRayResultCallback.*mTarget\(&target\).*m_flags\s*=\s*btTriangleRaycastCallback::kF_FilterBackfaces;.*proxy->m_clientObject\s*==\s*mTarget.*rayResult\.m_collisionObject\s*!=\s*mTarget)(?=.*bool\s+isWorldTriangleHit\(const\s+btCollisionWorld::LocalConvexResult&\s+convexResult\).*m_collisionFilterGroup\s*==\s*CollisionType_World.*m_localShapeInfo\s*!=\s*nullptr.*m_shapePart\s*>=\s*0.*m_triangleIndex\s*>=\s*0)(?=.*bool\s+hitsAuthoredFrontFace\(const\s+btCollisionWorld&\s+world,\s*const\s+btCollisionObject&\s+me,\s*const\s+btCollisionObject&\s+target,\s*const\s+btVector3&\s+hitPointWorld,\s*const\s+btVector3&\s+movement\).*FrontFaceRayCallback\s+callback\(me,\s*target,\s*from,\s*to\);.*world\.rayTest\(from,\s*to,\s*callback\);.*return\s+callback\.hasHit\(\);)(?=.*if\s*\(isWorldTriangleHit\(convexResult\).*!\s*hitsAuthoredFrontFace\(\s*\*mWorld,\s*\*mMe,\s*\*convexResult\.m_hitCollisionObject,\s*convexResult\.m_hitPointLocal,\s*-mMotion\)\)\s*return\s+1;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7067 TCL can target actors without changing normal script semantics" -Text ($compilerOpcodes + "`n" + $compilerExtensions + "`n" + $controlExtensions) `
    -Pattern '(?=.*opcodeToggleCollisionExplicit\s*=\s*0x2000327)(?=.*registerInstruction\("togglecollision",\s*"",\s*opcodeToggleCollision,\s*opcodeToggleCollisionExplicit\))(?=.*registerInstruction\("tcl",\s*"",\s*opcodeToggleCollision,\s*opcodeToggleCollisionExplicit\))(?=.*template\s*<class\s+R>\s*class\s+OpToggleCollision)(?=.*context\.getContextType\(\)\s*==\s*ScriptController::Console.*ptr\s*=\s*R\(\)\(runtime,\s*false,\s*true\);.*!ptr\.isEmpty\(\)\s*&&\s*!ptr\.getClass\(\)\.isActor\(\).*ptr\s*=\s*MWWorld::Ptr\(\);)(?=.*ptr\s*=\s*R\(\)\(runtime,\s*true,\s*true\);.*!ptr\.getClass\(\)\.isActor\(\).*Collision can only be toggled for actors.*return;)(?=.*ptr\.isEmpty\(\)\s*\|\|\s*ptr\s*==\s*world->getPlayerPtr\(\).*world->toggleCollisionMode\(\))(?=.*const\s+bool\s+enabled\s*=\s*!world->isActorCollisionEnabled\(ptr\);.*world->setActorCollisionMode\(ptr,\s*enabled,\s*enabled\);.*world->adjustPosition\(ptr,\s*true\);)(?=.*installSegment5<OpToggleCollision<ImplicitRef>>\(Compiler::Control::opcodeToggleCollision\))(?=.*installSegment5<OpToggleCollision<ExplicitRef>>\(Compiler::Control::opcodeToggleCollisionExplicit\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9115 menu Lua input callbacks persist across game loads" -Text ($inputBindings + "`n" + $inputActions + "`n" + $inputActionsHeader + "`n" + $inputActionsTest) `
    -Pattern '(?=.*api\["bindAction"\]\s*=\s*\[manager\s*=\s*context\.mLuaManager,\s*persistent\s*=\s*context\.mType\s*==\s*Context::Menu\].*inputActions\(\)\.bind\(key,\s*LuaUtil::Callback::fromLua\(callback\),\s*parsedDependencies,\s*persistent\))(?=.*api\["registerActionHandler"\]\s*=\s*\[manager\s*=\s*context\.mLuaManager,\s*persistent\s*=\s*context\.mType\s*==\s*Context::Menu\].*inputActions\(\)\.registerHandler\(key,\s*LuaUtil::Callback::fromLua\(callback\),\s*persistent\))(?=.*api\["registerTriggerHandler"\]\s*=\s*\[manager\s*=\s*context\.mLuaManager,\s*persistent\s*=\s*context\.mType\s*==\s*Context::Menu\].*inputTriggers\(\)\.registerHandler\(key,\s*LuaUtil::Callback::fromLua\(callback\),\s*persistent\))(?=.*struct\s+Handler\s*\{\s*LuaUtil::Callback\s+mCallback;\s*bool\s+mPersistent\s*=\s*false;)(?=.*void\s+Registry::clear\(bool\s+force\).*PersistentAction.*handler\.mPersistent\s*&&\s*handler\.mCallback\.isValid\(\).*binding\.mPersistent.*insert\(action\.mInfo\).*bind\(action\.mInfo\.mKey,\s*binding\.mCallback,\s*dependencies,\s*true\);)(?=.*void\s+Registry::clear\(bool\s+force\).*PersistentTrigger.*handler\.mPersistent\s*&&\s*handler\.mCallback\.isValid\(\).*insert\(trigger\.mInfo\).*mHandlers\[safeIdByKey\(trigger\.mInfo\.mKey\)\]\s*=\s*std::move\(trigger\.mHandlers\);)(?=.*PersistentActionCallbacksSurviveNonForcedClear)(?=.*PersistentTriggerHandlersSurviveNonForcedClear)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9073 Lua require cache is case-insensitive" -Text ($luaState + "`n" + $luaStateTest) `
    -Pattern '(?=.*function\s+requireGen\(env,\s*loaded,\s*loadFn\).*local\s+packageKey\s*=\s*string\.lower\(packageName\).*local\s+p\s*=\s*loaded\[packageKey\]\s+or\s+loaded\[packageName\].*loaded\[packageKey\]\s*=\s*p)(?=.*isCounterCacheCaseInsensitive\s*=\s*function\(\).*return\s+require\(''aaa\.counter''\)\s*==\s*require\(''AAA\.Counter''\).*EXPECT_TRUE\(LuaUtil::call\(script\["isCounterCacheCaseInsensitive"\]\)\.get<bool>\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9054 UI content indexes element children by layout name" -Text ($luaUiContent + "`n" + $luaUiContentTest) `
    -Pattern '(?=.*local\s+function\s+childName\(v\).*if\s+type\(v\.name\)\s*==\s*''string''\s+then\s*return\s+v\.name\s*end.*local\s+layout\s*=\s*v\.layout.*return\s+layout\s+and\s+type\(layout\.name\)\s*==\s*''string''\s+and\s+layout\.name)(?=.*content:add\(element\).*assert\(content:indexOf\(''elementName''\)\s*~=\s*nil,\s*''Could not find element by layout name''\).*assert\(content\[''elementName''\]\s*==\s*element,\s*''Could not access element by layout name''\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9050 UI focus events emitted during updates run after delayed actions" -Text ($luaUiWidget + "`n" + $luaUiWidgetHeader + "`n" + $luaUiElement + "`n" + $luaUiUtilHeader + "`n" + $luaUiUtil + "`n" + $luaManagerImp) `
    -Pattern '(?=.*class\s+DeferredFocusEventScope\s*\{.*DeferredFocusEventScope\(\);.*~DeferredFocusEventScope\(\);)(?=.*struct\s+WidgetExtension::DeferredFocusCallback\s*\{.*LuaUtil::Callback\s+mCallback;.*sol::main_table\s+mLayout;.*bool\s+mPropagateEvents;)(?=.*int\s+WidgetExtension::sDeferredFocusEventDepth\s*=\s*0;.*std::vector<WidgetExtension::DeferredFocusEvent>\s+WidgetExtension::sDeferredFocusEvents;)(?=.*void\s+WidgetExtension::propagateFocusEvent\(std::string_view\s+name\)\s+const.*if\s*\(!shouldDeferFocusEvents\(\)\).*propagateEvent\(name,\s*\[\]\(auto\)\s*\{\s*return\s+sol::nil;\s*\}\).*DeferredFocusEvent\s+event\{\s*std::string\(name\),\s*\{\}\s*\};.*event\.mCallbacks\.push_back\(\{\s*it->second,\s*w->mLayout,\s*w->mPropagateEvents\s*\}\);.*sDeferredFocusEvents\.push_back\(std::move\(event\)\);)(?=.*void\s+WidgetExtension::focusGain\(MyGUI::Widget\*,\s*MyGUI::Widget\*\).*mFocused\s*=\s*true;.*propagateFocusEvent\("focusGain"\);)(?=.*void\s+WidgetExtension::focusLoss\(MyGUI::Widget\*,\s*MyGUI::Widget\*\).*mFocused\s*=\s*false;.*propagateFocusEvent\("focusLoss"\);)(?=.*WidgetExtension::DeferredFocusEventScope\s+deferFocusEvents;)(?=.*void\s+flushDeferredFocusEvents\(\);.*void\s+flushDeferredFocusEvents\(\).*WidgetExtension::flushDeferredFocusEvents\(\);)(?=.*void\s+clearGameInterface\(\).*WidgetExtension::clearDeferredFocusEvents\(\);.*void\s+clearMenuInterface\(\).*WidgetExtension::clearDeferredFocusEvents\(\);)(?=.*void\s+LuaManager::applyDelayedActions\(\).*BoolScopeGuard\s+applyingGuard\(mApplyingDelayedActions\);.*mTeleportPlayerAction\.reset\(\);\s*\}\s*LuaUi::flushDeferredFocusEvents\(\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9051 UI destroy fires focusLoss for focused widgets" -Text ($uiBindings + "`n" + $luaUiWidget + "`n" + $luaUiWidgetHeader) `
    -Pattern '(?=.*void\s+fireFocusLossIfFocused\(\);)(?=.*bool\s+mFocused\s*=\s*false;)(?=.*void\s+WidgetExtension::fireFocusLossIfFocused\(\).*if\s*\(mFocused\)\s*propagateEvent\("focusLoss",\s*\[\]\(auto\)\s*\{\s*return\s+sol::nil;\s*\}\);.*for\s*\(WidgetExtension\*\s+w\s*:\s*mChildren\).*w->fireFocusLossIfFocused\(\);.*for\s*\(WidgetExtension\*\s+w\s*:\s*mTemplateChildren\).*w->fireFocusLossIfFocused\(\);)(?=.*void\s+WidgetExtension::focusGain\(MyGUI::Widget\*,\s*MyGUI::Widget\*\).*mFocused\s*=\s*true;.*propagateFocusEvent\("focusGain"\);)(?=.*void\s+WidgetExtension::focusLoss\(MyGUI::Widget\*,\s*MyGUI::Widget\*\).*mFocused\s*=\s*false;.*propagateFocusEvent\("focusLoss"\);)(?=.*uiElement\["destroy"\].*element->mRoot\s*!=\s*nullptr.*element->mRoot->fireFocusLossIfFocused\(\);.*luaManager->addAction\(\[element\]\s*\{\s*LuaUi::Element::erase\(element\.get\(\)\);\s*\},\s*"Destroy UI"\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9089 removed local scripts drop pending timers" -Text ($scriptsContainer + "`n" + $scriptsContainerHeader + "`n" + $scriptsContainerTest) `
    -Pattern '(?=.*static\s+void\s+removeTimers\(std::vector<Timer>&\s+timerQueue,\s*int\s+scriptId\);)(?=.*void\s+ScriptsContainer::removeScript\(int\s+scriptId\).*removeTimers\(data\.mSimulationTimersQueue,\s*scriptId\);.*removeTimers\(data\.mGameTimersQueue,\s*scriptId\);)(?=.*void\s+ScriptsContainer::removeTimers\(std::vector<Timer>&\s+timerQueue,\s*int\s+scriptId\).*std::remove_if\(timerQueue\.begin\(\),\s*timerQueue\.end\(\).*timer\.mScriptId\s*==\s*scriptId.*std::make_heap\(timerQueue\.begin\(\),\s*timerQueue\.end\(\)\);)(?=.*RemoveScriptDropsTimers.*setupSerializableTimer\(.*TimerType::SIMULATION_TIME,\s*5,\s*removedScriptId.*setupUnsavableTimer\(TimerType::GAME_TIME,\s*5,\s*removedScriptId.*scripts\.removeScript\(removedScriptId\);.*scripts\.processTimers\(10,\s*10\);.*EXPECT_EQ\(removedSerializableCounter,\s*0\);.*EXPECT_EQ\(removedUnsavableCounter,\s*0\);.*EXPECT_EQ\(keptSerializableCounter,\s*3\);.*EXPECT_EQ\(keptUnsavableCounter,\s*1\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9065 exposes createRecordDraft for more custom record types" -Text ($ingredientBindings + "`n" + $lockpickBindings + "`n" + $repairBindings + "`n" + $apparatusBindings + "`n" + $soundBindings + "`n" + $typesLua + "`n" + $coreLua + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*ingredient\["createRecordDraft"\]\s*=\s*tableToIngredient;)(?=.*lockpick\["createRecordDraft"\]\s*=\s*tableToLockpick;)(?=.*repair\["createRecordDraft"\]\s*=\s*tableToRepair;)(?=.*apparatus\["createRecordDraft"\]\s*=\s*tableToApparatus;)(?=.*api\["createRecordDraft"\]\s*=\s*tableToSound;)(?=.*@function \[parent=#Ingredient\] createRecordDraft)(?=.*@function \[parent=#Lockpick\] createRecordDraft)(?=.*@function \[parent=#Repair\] createRecordDraft)(?=.*@function \[parent=#Apparatus\] createRecordDraft)(?=.*@function \[parent=#Sound\] createRecordDraft)(?=.*types\.Lockpick\.createRecordDraft\(newLockpick\))(?=.*types\.Repair\.createRecordDraft\(newRepair\))(?=.*types\.Ingredient\.createRecordDraft\(newIngredient\))(?=.*types\.Apparatus\.createRecordDraft\(newApparatus\))(?=.*core\.sound\.createRecordDraft\(newSound\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9066 keeps openmw.content names consistent with types and core" -Text ($contentBindings + "`n" + $contentLua + "`n" + $luaApiLoadTest) `
    -Pattern '(?=.*api\["activators"\]\s*=\s*initActivatorBindings)(?=.*api\["books"\]\s*=\s*initBookBindings)(?=.*api\["doors"\]\s*=\s*initDoorBindings)(?=.*api\["ingredients"\]\s*=\s*initIngredientBindings)(?=.*api\["lights"\]\s*=\s*initLightBindings)(?=.*api\["miscs"\]\s*=\s*initMiscBindings)(?=.*api\["potions"\]\s*=\s*initPotionBindings)(?=.*api\["probes"\]\s*=\s*initProbeBindings)(?=.*api\["statics"\]\s*=\s*initStaticBindings)(?=.*api\["sounds"\]\s*=\s*initSoundBindings)(?=.*api\["gameSettings"\]\s*=\s*initGameSettingBindings)(?=.*@field \[parent=#content\] #ActivatorContent activators)(?=.*@field \[parent=#content\] #BookContent books)(?=.*@field \[parent=#content\] #DoorContent doors)(?=.*@field \[parent=#content\] #IngredientContent ingredients)(?=.*@field \[parent=#content\] #LightContent lights)(?=.*@field \[parent=#content\] #MiscContent miscs)(?=.*@field \[parent=#content\] #PotionContent potions)(?=.*@field \[parent=#content\] #ProbeContent probes)(?=.*@field \[parent=#content\] #StaticContent statics)(?=.*@field \[parent=#content\] #SoundContent sounds)(?=.*@field \[parent=#content\] #GMSTContent gameSettings)(?=.*expectContentStore\(name\))(?=.*''activators''.*''books''.*''doors''.*''ingredients''.*''lights''.*''miscs''.*''potions''.*''probes''.*''statics''.*''sounds''.*''gameSettings'')(?=.*expectNoContentStore\(name\))(?=.*''activator''.*''book''.*''door''.*''ingredient''.*''light''.*''miscellaneous''.*''potion''.*''probe''.*''static''.*''sound''.*''gmsts'')' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8958 Lua can correlate built-in windows with MyGUI layers" -Text ($windowManagerBase + "`n" + $windowManagerImpHeader + "`n" + $windowManagerImp + "`n" + $uiBindings + "`n" + $uiLua + "`n" + $luaApiPlayerTest) `
    -Pattern '(?=.*virtual\s+std::optional<std::string>\s+getWindowLayer\(std::string_view\s+windowId\)\s+const\s*=\s*0;)(?=.*std::optional<std::string>\s+getWindowLayer\(std::string_view\s+windowId\)\s+const\s+override;)(?=.*std::optional<std::string>\s+WindowManager::getWindowLayer\(std::string_view\s+windowId\)\s+const.*mLuaIdToWindow\.find\(windowId\).*Invalid window name.*mMainWidget.*getLayer\(\).*layer->getName\(\))(?=.*api\["_getLayerForWindow"\].*sol::optional<std::string>.*getWindowLayer\(window\))(?=.*local\s+function\s+getLayerForWindow\(windowName\).*WINDOW\[windowName\].*Unknown window.*ui\._getLayerForWindow\(windowName\))(?=.*version\s*=\s*4)(?=.*getLayerForWindow\s*=\s*getLayerForWindow)(?=.*registerLocalTest\(''window layers are exposed''.*I\.UI\.version,\s*4.*I\.UI\.getLayerForWindow\(I\.UI\.WINDOW\.Map\),\s*''Windows''.*I\.UI\.getLayerForWindow\(I\.UI\.WINDOW\.Inventory\),\s*''Windows''.*NotAWindow)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9109 expands openmw.content with equipment, container, creature, and NPC record stores" -Text ($contentBindings + "`n" + $apparatusBindings + "`n" + $armorBindings + "`n" + $clothingBindings + "`n" + $containerBindings + "`n" + $creatureBindings + "`n" + $npcBindings + "`n" + $weaponBindings + "`n" + $contentLua + "`n" + $luaApiLoadTest + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*api\["apparatuses"\]\s*=\s*initApparatusBindings)(?=.*api\["armors"\]\s*=\s*initArmorBindings)(?=.*api\["clothing"\]\s*=\s*initClothingBindings)(?=.*api\["containers"\]\s*=\s*initContainerBindings)(?=.*api\["creatures"\]\s*=\s*initCreatureBindings)(?=.*api\["npcs"\]\s*=\s*initNpcBindings)(?=.*api\["weapons"\]\s*=\s*initWeaponBindings)(?=.*addMutableApparatusType\(lua\))(?=.*addMutableArmorType\(lua\))(?=.*addMutableClothingType\(lua\))(?=.*addMutableContainerType\(lua\))(?=.*addMutableCreatureType\(lua\))(?=.*addMutableNpcType\(lua\))(?=.*addMutableWeaponType\(lua\))(?=.*content\.apparatuses\.records\.OMW_Generated_Apparatus)(?=.*content\.armors\.records\.OMW_Generated_Armor)(?=.*content\.clothing\.records\.OMW_Generated_Clothing)(?=.*content\.containers\.records\.OMW_Generated_Container)(?=.*content\.creatures\.records\.OMW_Generated_Creature)(?=.*content\.npcs\.records\.OMW_Generated_NPC)(?=.*content\.weapons\.records\.OMW_Generated_Weapon)(?=.*load script generated content records)(?=.*@field \[parent=#content\] #ApparatusContent apparatuses)(?=.*@field \[parent=#content\] #ArmorContent armors)(?=.*@field \[parent=#content\] #ClothingContent clothing)(?=.*@field \[parent=#content\] #ContainerContent containers)(?=.*@field \[parent=#content\] #CreatureContent creatures)(?=.*@field \[parent=#content\] #NpcContent npcs)(?=.*@field \[parent=#content\] #WeaponContent weapons)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8948 exposes corpse persistence flags on Creature and NPC Lua records" -Text ($creatureBindings + "`n" + $npcBindings + "`n" + $typesLua + "`n" + $luaApiLoadTest + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*rec\["isPersistent"\]\s*!=\s*sol::nil.*bool\s+persistent\s*=\s*rec\["isPersistent"\].*crea\.mRecordFlags\s*\|=\s*ESM::FLAG_Persistent.*crea\.mRecordFlags\s*&=\s*~ESM::FLAG_Persistent)(?=.*Types::addFlagProperty\(record,\s*"isPersistent",\s*ESM::FLAG_Persistent,\s*&ESM::Creature::mRecordFlags\);)(?=.*record\["isPersistent"\]\s*=\s*sol::readonly_property\(.*const\s+ESM::Creature&\s+rec.*rec\.mRecordFlags\s*&\s*ESM::FLAG_Persistent)(?=.*rec\["isPersistent"\]\s*!=\s*sol::nil.*bool\s+persistent\s*=\s*rec\["isPersistent"\].*npc\.mRecordFlags\s*\|=\s*ESM::FLAG_Persistent.*npc\.mRecordFlags\s*&=\s*~ESM::FLAG_Persistent)(?=.*Types::addFlagProperty\(record,\s*"isPersistent",\s*ESM::FLAG_Persistent,\s*&ESM::NPC::mRecordFlags\);)(?=.*record\["isPersistent"\]\s*=\s*sol::readonly_property\(.*const\s+ESM::NPC&\s+rec.*rec\.mRecordFlags\s*&\s*ESM::FLAG_Persistent)(?=.*@field #boolean isPersistent If true, the creature will not despawn after death\.)(?=.*@field #boolean isPersistent If true, the NPC will not despawn after death\.)(?=.*content\.creatures\.records\.OMW_Generated_Creature\s*=\s*\{.*isPersistent\s*=\s*true)(?=.*content\.npcs\.records\.OMW_Generated_NPC\s*=\s*\{.*isPersistent\s*=\s*true)(?=.*testing\.expectEqual\(creature\.isPersistent,\s*true\))(?=.*testing\.expectEqual\(npc\.isPersistent,\s*true\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8923 exposes NPC breath timer to Lua" -Text ($npcBindings + "`n" + $typesLua + "`n" + $luaApiGlobalTest) `
    -Pattern '(?=.*#include\s+<components/misc/finitevalues\.hpp>)(?=.*npc\["getBreathTimer"\]\s*=\s*\[\]\(const\s+Object&\s+o\)\s*->\s*float.*verifyNpc\(cls\).*getNpcStats\(ptr\)\.getTimeToStartDrowning\(\))(?=.*npc\["setBreathTimer"\]\s*=\s*\[\]\(Object&\s+o,\s*Misc::FiniteFloat\s+timeLeft\).*Local scripts can modify only self.*verifyNpc\(cls\).*getNpcStats\(ptr\)\.setTimeToStartDrowning\(timeLeft\))(?=.*@function \[parent=#NPC\] getBreathTimer.*@function \[parent=#NPC\] setBreathTimer)(?=.*testing\.registerGlobalTest\(''npc breath timer''.*types\.NPC\.setBreathTimer\(player,\s*7\.25\).*types\.NPC\.getBreathTimer\(player\),\s*7\.25.*math\.huge.*Value must be a finite number)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9100 adds a search filter to the save and load dialog" -Text ($saveGameDialog + "`n" + $saveGameDialogHeader + "`n" + $saveGameDialogLayout) `
    -Pattern '(?=.*<Widget\s+type="EditBox"\s+skin="MW_TextBoxEditWithBorder"\s+position="0 0 0 23"\s+name="FilterEdit">)(?=.*MyGUI::EditBox\*\s+mFilterEdit;)(?=.*getWidget\(mFilterEdit,\s*"FilterEdit"\);)(?=.*mFilterEdit->eventEditTextChange\s*\+=\s*MyGUI::newDelegate\(this,\s*&SaveGameDialog::onFilterChanged\);)(?=.*void\s+SaveGameDialog::onFilterChanged\(MyGUI::EditBox\*\s*/\*sender\*/\).*fillSaveList\(\);)(?=.*bool\s+SaveGameDialog::slotMatchesFilter\(const\s+MWState::Slot&\s+slot\)\s+const.*mFilterEdit->getOnlyText\(\)\.asUTF8\(\).*slot\.mProfile\.mDescription.*slot\.mProfile\.mPlayerName.*slot\.mProfile\.mPlayerCellName.*Files::pathToUnicodeString\(slot\.mPath\.filename\(\)\))(?=.*mSaveList->addItem\(it->mProfile\.mDescription,\s*&\*it\);)(?=.*mCurrentSlot\s*=\s*\*sender->getItemDataAt<const\s+MWState::Slot\*>\(pos\);)(?=.*!Settings::gui\(\)\.mControllerMenus.*setKeyFocusWidget\(mFilterEdit\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #9082 restores active Sound magic effect loops after loading" -Text $spellEffects `
    -Pattern '(?=.*void\s+ensureMagicSoundLoop\(const\s+MWWorld::Ptr&\s+target\).*target\s*!=\s*MWMechanics::getPlayer\(\).*getSoundPlaying\(target,\s*magicSound\).*getMagicEffects\(\).*getOrDefault\(ESM::MagicEffect::Sound\)\.getMagnitude\(\).*playSound3D\(\s*target,\s*magicSound,\s*volume,\s*1\.f,\s*MWSound::Type::Sfx,\s*MWSound::PlayMode::LoopNoEnv\))(?=.*effect\.mFlags\s*&\s*ESM::ActiveEffect::Flag_Applied.*else\s+if\s*\(!dt\)\s*\{\s*if\s*\(effect\.mEffectId\s*==\s*ESM::MagicEffect::Sound\)\s*ensureMagicSoundLoop\(target\);\s*return\s+\{\s*MagicApplicationResult::Type::APPLIED)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7995 OpenMW-CS ignores stale deleted instance subviews and scene hints" -Text ($opencsView + "`n" + $opencsPagedWorldspaceWidget) `
    -Pattern '(?=.*bool\s+isUnavailableReferenceSubview\(CSMDoc::Document&\s+document,\s+const\s+CSMWorld::UniversalId&\s+id\).*id\.getType\(\)\s*!=\s*CSMWorld::UniversalId::Type_Reference.*references\.getModelIndex\(id\.getId\(\),\s*stateColumn\).*!index\.isValid\(\).*CSMWorld::RecordBase::State_Deleted)(?=.*void\s+CSVDoc::View::addSubView.*if\s*\(isUnavailableReferenceSubview\(\*mDocument,\s*id\)\)\s*return;)(?=.*QModelIndex\s+cellIndex\s*=\s*references\.getModelIndex\(refCode,\s*cellColumn\);.*QModelIndex\s+stateIndex\s*=\s*references\.getModelIndex\(refCode,\s*stateColumn\);.*!cellIndex\.isValid\(\)\s*\|\|\s*!stateIndex\.isValid\(\).*CSMWorld::RecordBase::State_Deleted.*return;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8784 OpenMW-CS registers a Distortion render bin fallback" -Text $opencsSceneWidget `
    -Pattern '(?=.*#include\s+<osgUtil/RenderBin>)(?=.*#include\s+<components/sceneutil/depth\.hpp>)(?=.*void\s+ensureRenderBinPrototypes\(\).*static\s+const\s+bool\s+prototypesAdded.*new\s+osgUtil::RenderBin\(osgUtil::RenderBin::SORT_BACK_TO_FRONT\).*setAttributeAndModes\(new\s+SceneUtil::AutoDepth,\s*osg::StateAttribute::ON\s*\|\s*osg::StateAttribute::OVERRIDE\).*osgUtil::RenderBin::addRenderBinPrototype\("Distortion",\s*distortionRenderBin\))(?=.*RenderWidget::RenderWidget.*ensureRenderBinPrototypes\(\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8634 OpenMW-CS selection marker stays unlit in lighting preview modes" -Text $opencsObjectMarker `
    -Pattern '(?=.*#include\s+<osg/GL>)(?=.*#include\s+<osg/StateAttribute>)(?=.*recreateShaders\(mBaseNode,\s*"debug"\);)(?=.*baseNodeState->setMode\(\s*GL_LIGHTING,\s*osg::StateAttribute::OFF\s*\|\s*osg::StateAttribute::PROTECTED\s*\|\s*osg::StateAttribute::OVERRIDE\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8626 OpenMW-CS cell arrows are color-coded by direction" -Text $opencsCellArrow `
    -Pattern '(?=.*osg::Vec4f\s+darken\(const\s+osg::Vec4f&\s+colour\).*constexpr\s+float\s+factor\s*=\s*0\.7f)(?=.*osg::Vec4f\s+getDirectionColour\(CSVRender::CellArrow::Direction\s+direction\).*Direction_North.*0\.12f,\s*0\.72f,\s*0\.28f.*Direction_West.*0\.54f,\s*0\.36f,\s*0\.86f.*Direction_South.*0\.86f,\s*0\.25f,\s*0\.20f.*Direction_East.*0\.95f,\s*0\.66f,\s*0\.14f)(?=.*const\s+osg::Vec4f\s+colour\s*=\s*getDirectionColour\(mDirection\);.*const\s+osg::Vec4f\s+shadedColour\s*=\s*darken\(colour\);.*colours->push_back\(colour\);.*colours->push_back\(shadedColour\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8635 OpenMW-CS selection marker picking has screen-space tolerance" -Text ($opencsWorldspaceWidgetHeader + "`n" + $opencsWorldspaceWidget) `
    -Pattern '(?=.*#include\s+<osgUtil/PolytopeIntersector>)(?=.*std::optional<WorldspaceHitResult>\s+checkTagAtWorldPos\(\s*const\s+osg::NodePath&\s+nodePath,\s*const\s+osg::Vec3d&\s+worldPos\)\s+const;)(?=.*std::optional<WorldspaceHitResult>\s+mousePickMarker\(const\s+QPoint&\s+localPos,\s*unsigned\s+int\s+interactionMask\)\s+const;)(?=.*WorldspaceWidget::checkTagAtWorldPos\(.*return\s+WorldspaceHitResult\{\s*true,\s*tag,\s*0,\s*0,\s*0,\s*worldPos\s*\};)(?=.*WorldspaceWidget::checkTag\(.*checkTagAtWorldPos<Tag>\(intersection\.nodePath,\s*intersection\.getWorldIntersectPoint\(\)\).*intersection\.indexList\.size\(\)\s*>=\s*3.*hit->index0\s*=\s*intersection\.indexList\[0\];)(?=.*constexpr\s+double\s+markerPickRadius\s*=\s*6\.0;.*new\s+osgUtil::PolytopeIntersector\(\s*osgUtil::Intersector::WINDOW,\s*x\s*-\s*radius,\s*y\s*-\s*radius,\s*x\s*\+\s*radius,\s*y\s*\+\s*radius\).*visitor\.setTraversalMask\(interactionMask\).*worldPos\s*=\s*worldPos\s*\*\s*\*intersection\.matrix;.*checkTagAtWorldPos<ObjectMarkerTag>\(intersection\.nodePath,\s*worldPos\).*hitBehindMarker\(markerHit->worldPos,\s*mView->getCamera\(\)\).*return\s+markerHit;)(?=.*WorldspaceWidget::mousePick\(.*if\s*\(const\s+auto\s+markerHit\s*=\s*mousePickMarker\(localPos,\s*interactionMask\)\)\s*return\s+\*markerHit;)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #6778 OpenMW-CS verifier warns about long record IDs" -Text ($openCsTools + "`n" + $openCsRecordIdCheckHeader + "`n" + $openCsRecordIdCheck + "`n" + $openCsRecordIdCheckTest) `
    -Pattern '(?=.*RecordIdCheckStage)(?=.*sMaxLegacyRecordIdLength\s*=\s*32)(?=.*stringId->getValue\(\)\.size\(\)\s*<=\s*CSMTools::RecordIdCheckStage::sMaxLegacyRecordIdLength)(?=.*Record ID is longer than 32 bytes; Morrowind and legacy ESM3 fields may truncate or reject it)(?=.*CSMDoc::Message::Severity_Warning)(?=.*getRecord\(index\).*record\.isDeleted\(\))(?=.*getIf<ESM::StringRefId>\(\))(?=.*shouldWarnAboutStringRecordIdsLongerThanThirtyTwoBytes)(?=.*shouldIgnoreDeletedAndNonStringRecordIds)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8344 OpenMW-CS pathgrid node deletion removes invalid stale edges" -Text $opencsPathgrid `
    -Pattern '(?=.*const\s+size_t\s+pointCount\s*=\s*source->mPoints\.size\(\);)(?=.*const\s+size_t\s+remainingPointCount\s*=\s*mSelected\.size\(\)\s*<\s*pointCount\s*\?\s*pointCount\s*-\s*mSelected\.size\(\)\s*:\s*0;)(?=.*bool\s+removeEdge\s*=\s*false;.*source->mEdges\[edge\]\.mV0\s*==\s*point\s*\|\|\s*source->mEdges\[edge\]\.mV1\s*==\s*point.*removeEdge\s*=\s*true;.*break;)(?=.*const\s+size_t\s+adjustedEdge0\s*=\s*source->mEdges\[edge\]\.mV0\s*-\s*adjustment0;.*const\s+size_t\s+adjustedEdge1\s*=\s*source->mEdges\[edge\]\.mV1\s*-\s*adjustment1;)(?=.*if\s*\(removeEdge\s*\|\|\s*adjustedEdge0\s*>=\s*remainingPointCount\s*\|\|\s*adjustedEdge1\s*>=\s*remainingPointCount\).*edgeRowsToRemove\.insert\(static_cast<int>\(edge\)\);.*continue;)(?=.*static_cast<int>\(adjustedEdge0\))(?=.*static_cast<int>\(adjustedEdge1\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8022 OpenMW-CS modify commands keep persistent model indexes" -Text ($opencsCommandsHeader + "`n" + $opencsCommands) `
    -Pattern '(?=.*#include\s+<QPersistentModelIndex>)(?=.*QPersistentModelIndex\s+mIndex;)(?=.*QPersistentModelIndex\s+mRecordStateIndex;)(?=.*QModelIndex\s+sourceIndex\s*=\s*index;.*sourceIndex\s*=\s*proxy->mapToSource\(sourceIndex\).*mIndex\s*=\s*sourceIndex;)(?=.*void\s+CSMWorld::ModifyCommand::redo\(\).*const\s+QModelIndex\s+index\s*=\s*mIndex;.*!mModel\s*\|\|\s*!index\.isValid\(\).*return;.*const\s+QModelIndex\s+parent\s*=\s*index\.parent\(\);.*dynamic_cast<CSMWorld::IdTree\*>\(mModel\).*mOld\s*=\s*mModel->data\(index,\s*Qt::EditRole\);.*mModel->setData\(index,\s*mNew\);)(?=.*void\s+CSMWorld::ModifyCommand::undo\(\).*const\s+QModelIndex\s+index\s*=\s*mIndex;.*!mModel\s*\|\|\s*!index\.isValid\(\).*return;.*mModel->setData\(index,\s*mOld\).*const\s+QModelIndex\s+recordStateIndex\s*=\s*mRecordStateIndex;.*recordStateIndex\.isValid\(\))' `
    -Missing $missing

Test-Pattern -Name "OpenMW #2599 OpenMW-CS marks viewed instances in scene view" -Text ($opencsWorldspaceWidgetHeader + "`n" + $opencsWorldspaceWidget + "`n" + $opencsPagedWorldspaceWidget) `
    -Pattern '(?=.*bool\s+selectReferenceById\(const\s+std::string&\s+referenceId\);)(?=.*void\s+CSVRender::WorldspaceWidget::useViewHint\(const\s+std::string&\s+hint\).*hint\.rfind\("r:",\s*0\)\s*==\s*0.*selectReferenceById\(hint\.substr\(2\)\);)(?=.*bool\s+CSVRender::WorldspaceWidget::selectReferenceById\(const\s+std::string&\s+referenceId\).*CSVRender::Object\*\s+object\s*=\s*getObjectByReferenceId\(referenceId\).*clearSelection\(Mask_Reference\);.*object->setSelected\(true\);.*mSelectionMarker->addToSelectionHistory\(object->getReferenceId\(\),\s*false\);.*mSelectionMarker->updateSelectionMarker\(object->getReferenceId\(\)\);)(?=.*void\s+CSVRender::PagedWorldspaceWidget::useViewHint\(const\s+std::string&\s+hint\).*std::string\s+referenceToSelect;.*referenceToSelect\s*=\s*refCode;.*setCellSelection\(selection\);.*if\s*\(!referenceToSelect\.empty\(\)\)\s*selectReferenceById\(referenceToSelect\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #3429 OpenMW-CS reuses script editor subviews for script records" -Text ($opencsView + "`n" + $opencsPrefsState + "`n" + $opencsScriptSubViewHeader + "`n" + $opencsScriptSubView) `
    -Pattern '(?=.*void\s+switchToIdAndUseHint\(const\s+std::string&\s+id,\s*const\s+std::string&\s+hint\);)(?=.*void\s+CSVWorld::ScriptSubView::switchToIdAndUseHint\(const\s+std::string&\s+id,\s*const\s+std::string&\s+hint\).*QModelIndex\s+index\s*=\s*mModel->getModelIndex\(id,\s*0\);.*if\s*\(!index\.isValid\(\)\)\s*return;.*switchToRow\(index\.row\(\)\);.*if\s*\(!hint\.empty\(\)\)\s*useHint\(hint\);.*else\s*mEditor->setFocus\(\);)(?=.*if\s*\(windows\["reuse"\]\.isTrue\(\)\).*if\s*\(id\.getType\(\)\s*==\s*CSMWorld::UniversalId::Type_Script\).*CSVWorld::ScriptSubView\*\s+scriptView\s*=\s*dynamic_cast<CSVWorld::ScriptSubView\*>\(sb\);.*scriptView->switchToIdAndUseHint\(id\.getId\(\),\s*hint\);.*scriptView->setFocus\(\);.*return;)(?=.*Script record requests can retarget an existing script editor)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #8391 OpenMW-CS reuses same-type record editor subviews" -Text ($opencsView + "`n" + $opencsPrefsState + "`n" + $opencsDialogueSubViewHeader + "`n" + $opencsDialogueSubView) `
    -Pattern '(?=.*CSMWorld::UniversalId::Type\s+getDialogueReuseType\(const\s+CSMWorld::UniversalId&\s+id\).*Class_RefRecord.*Type_Referenceable.*return\s+id\.getType\(\);)(?=.*void\s+switchToIdAndUseHint\(const\s+CSMWorld::UniversalId&\s+id,\s*const\s+std::string&\s+hint\);)(?=.*void\s+CSVWorld::DialogueSubView::switchToIdAndUseHint\(const\s+CSMWorld::UniversalId&\s+id,\s*const\s+std::string&\s+hint\).*QModelIndex\s+index\s*=\s*getTable\(\)\.getModelIndex\(id\.getId\(\),\s*idColumn\);.*if\s*\(!index\.isValid\(\)\)\s*return;.*switchToRow\(index\.row\(\)\);.*if\s*\(!hint\.empty\(\)\)\s*useHint\(hint\);.*else\s*getEditWidget\(\)\.setFocus\(\);)(?=.*const\s+CSMWorld::UniversalId::Type\s+dialogueReuseType\s*=\s*getDialogueReuseType\(id\);.*CSVWorld::DialogueSubView\*\s+dialogueView\s*=\s*dynamic_cast<CSVWorld::DialogueSubView\*>\(sb\);.*getDialogueReuseType\(dialogueView->getUniversalId\(\)\)\s*==\s*dialogueReuseType.*dialogueView->switchToIdAndUseHint\(id,\s*hint\);.*dialogueView->setFocus\(\);.*return;)(?=.*Record edit requests can retarget an existing editor for the same record type\.)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7907 OpenMW-CS warns before creating addon without masters" -Text $opencsFileDialog `
    -Pattern '(?=.*#include\s+<QMessageBox>)(?=.*void\s+CSVDoc::FileDialog::slotNewFile\(\).*mSelector->selectedFiles\(\)\.empty\(\).*QMessageBox\s+warning\(this\).*Create addon without masters\?.*No master files are selected\..*Dependency-free addons can override default records and break game data\..*Create Anyway.*warning\.clickedButton\(\)\s*!=\s*createAnyway.*return;.*emit\s+signalCreateNewFile\(mAdjusterWidget->getPath\(\)\);)' `
    -Missing $missing

Test-Pattern -Name "OpenMW #7438 OpenMW-CS door teleport cell completion suggests only interiors" -Text ($opencsColumnBaseHeader + "`n" + $opencsColumnBase + "`n" + $opencsColumnImpHeader + "`n" + $opencsIdCompletionManager + "`n" + $opencsIdCompletionDelegate + "`n" + $opencsTableMimeData + "`n" + $opencsTestsCmake + "`n" + $opencsIdCompletionManagerTest) `
    -Pattern '(?=.*Display_InteriorCell)(?=.*Display_InteriorCell,\s*Display_Referenceable)(?=.*Display_Cell,\s*Display_InteriorCell,\s*Display_Referenceable)(?=.*ColumnId_TeleportCell,\s*ColumnBase::Display_InteriorCell)(?=.*class\s+InteriorCellCompletionModel\s*:\s*public\s+QSortFilterProxyModel)(?=.*filterAcceptsRow\(int\s+sourceRow,\s*const\s+QModelIndex&\s+sourceParent\)\s+const\s+override.*model->index\(sourceRow,\s*mCellParentColumn,\s*sourceParent\).*model->index\(0,\s*mInteriorColumn,\s*cellParent\).*Qt::EditRole.*toBool\(\))(?=.*types\[CSMWorld::ColumnBase::Display_InteriorCell\]\s*=\s*CSMWorld::UniversalId::Type_Cell)(?=.*findNestedColumnIndex\(cellParentColumn,\s*CSMWorld::Columns::ColumnId_Interior\))(?=.*display\s*==\s*CSMWorld::ColumnBase::Display_Cell\s*\|\|\s*display\s*==\s*CSMWorld::ColumnBase::Display_InteriorCell)(?=.*Type_Cell,\s*CSMWorld::ColumnBase::Display_InteriorCell)(?=.*testidcompletionmanager\.cpp)(?=.*Contains\(ColumnBase::Display_InteriorCell\))' `
    -Missing $missing

Write-Host "OpenMW upstream work item sync check"
Write-Host "Source root: $SourceRoot"
Write-Host "Guards checked: $guardCount"
Write-Host "Missing guards: $($missing.Count)"

foreach ($name in $missing) {
    Write-Host " - $name"
}

if ($FailOnMissingGuard -and $missing.Count -gt 0) {
    exit 1
}
