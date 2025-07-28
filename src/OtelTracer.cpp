#include "OtelTracer.h"

// Static vars
OTelResourceConfig OTelTracer::_resource = OTel::getDefaultResource();
String OTelTracer::_spanName = "";
uint64_t OTelTracer::_spanStart = 0;

