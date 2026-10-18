#include "ReloadableNPCEventHandler.h"

#include "L2Core/src/inc/Guard.h"
#include "ParamProperty.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/ThreadManager.h"


namespace {

CNPCEventHandler* InterlockedReadHandler(CNPCEventHandler* volatile* handler) {
    return runtime::raw_atomic::CompareExchangePointer(
        handler, static_cast<CNPCEventHandler*>(nullptr), static_cast<CNPCEventHandler*>(nullptr)
    );
}

} // namespace

CReloadableNPCEventHandler::CReloadableNPCEventHandler(const char16_t* aiClassName, CNPCEventHandler* eventHandler) :
    m_pRealHandler(eventHandler), m_sAIClassName(aiClassName) {}

CNPCEventHandler* CReloadableNPCEventHandler::InterlockedGetEventHandler() {
    L2_GUARD_FUNCTION();
    return InterlockedReadHandler(&m_pRealHandler);
}

void CReloadableNPCEventHandler::InterlockedSetEventHandler(CNPCEventHandler* eventHandler) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangePointer(&m_pRealHandler, eventHandler);
}

int CReloadableNPCEventHandler::InterlockedHandleEvent(ScriptEvent* event, CAIParameter* parameter) {
    L2_GUARD_FUNCTION();
    CNPCEventHandler* const handler = InterlockedReadHandler(&m_pRealHandler);
    return handler ? handler->HandleEvent(event, parameter) : 0;
}

CAIParameter* CReloadableNPCEventHandler::GetParameter() {
    L2_GUARD_FUNCTION();
    CNPCEventHandler* const handler = InterlockedReadHandler(&m_pRealHandler);
    return handler ? handler->m_pParameter : nullptr;
}

AIConstantData CReloadableNPCEventHandler::GetParameterData(const char16_t* name) {
    L2_GUARD_FUNCTION();
    CAIParameter* const parameter = GetParameter();
    return parameter ? parameter->GetData(name) : AIConstantData{};
}
