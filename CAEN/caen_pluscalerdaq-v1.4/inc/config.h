#pragma once

#ifndef __CONFIG__
#define __CONFIG__

#include "PLUscalerLib.h"

int ReadConfigFile(PLUscalerDescr *des, char *cfg_file);
void DefaultConfig(PLUscalerDescr *des);

#endif // __CONFIG__
