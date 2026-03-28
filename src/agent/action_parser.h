#ifndef AGENT_ACTION_PARSER_H
#define AGENT_ACTION_PARSER_H

#include <string>

#include "action.h"

Action parse_action_response(const std::string& response);

#endif
