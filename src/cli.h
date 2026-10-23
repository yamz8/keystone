#pragma once

#include <QStringList>

// Keystone's commands, which read a model without a window so agents and
// scripts can use them: info, elements, properties, skill, help. Each takes
// --json, writes errors to stderr, and exits 0 or 1. Hype's src/cli.cpp is
// the pattern.
bool isCliCommand(const char *word);
int runCli(const QStringList &arguments);
