#include "Settings/UnrealAzerothSettings.h"

#include "Internationalization/Text.h"

UUnrealAzerothSettings::UUnrealAzerothSettings()
{
    AuthServerHost = TEXT("127.0.0.1");
    AuthServerPort = 3724;
}

FName UUnrealAzerothSettings::GetCategoryName() const
{
    return TEXT("Plugins");
}

#if WITH_EDITOR
FText UUnrealAzerothSettings::GetSectionText() const
{
    return NSLOCTEXT("UnrealAzeroth", "SettingsSection", "Unreal Azeroth");
}
#endif
