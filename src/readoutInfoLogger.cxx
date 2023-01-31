// instantiation of the global variables defined in readoutInfoLogger.h
// to be included a single time in each executable

#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>

using namespace AliceO2::InfoLogger;
InfoLogger::AutoMuteToken theLogTokenCodeWrong(LogWarningDevel_(3244), 10, 300); // a dedicated token to report code that should never be executed

