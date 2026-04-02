#pragma once

#include "Modules/ModuleInterface.h"

class FUnrealAzerothModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
