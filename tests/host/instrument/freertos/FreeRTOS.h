#pragma once
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline unsigned fake_critical_entries=0;
#define portENTER_CRITICAL(mux) ((void)(mux),++fake_critical_entries)
#define portEXIT_CRITICAL(mux) ((void)(mux))
