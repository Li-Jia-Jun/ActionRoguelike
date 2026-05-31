


#include "RougeGameMode.h"
#include "Player/RoguePlayerController.h"


ARougeGameMode::ARougeGameMode()
{
	PlayerControllerClass = ARoguePlayerController::StaticClass();
}