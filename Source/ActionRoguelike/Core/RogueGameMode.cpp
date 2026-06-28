


#include "RogueGameMode.h"
#include "Player/RoguePlayerController.h"


ARogueGameMode::ARogueGameMode()
{
	PlayerControllerClass = ARoguePlayerController::StaticClass();
}