/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2014-2016 Symless Ltd.
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common/stdvector.h"

#include <string>

class ServerArgs;
class ClientArgs;
class ArgsBase;
class App;

class ArgParser {

public:
    ArgParser(App* app);

    bool parseServerArgs(ServerArgs& args, int argc, const char* const* argv);
    bool parseClientArgs(ClientArgs& args, int argc, const char* const* argv);
    bool parsePlatformArg(ArgsBase& argsBase, const int& argc, const char* const* argv, int& i);
    bool parseGenericArgs(int argc, const char* const* argv, int& i);
    void setArgsBase(ArgsBase& argsBase) { m_argsBase = &argsBase; }

    static bool isArg(int argi, int argc, const char* const* argv,
                            const char* name1, const char* name2,
                            int minRequiredParameters = 0);
    static void splitCommandString(std::string& command, std::vector<std::string>& argv);
    static bool searchDoubleQuotes(std::string& command, size_t& left,
                            size_t& right, size_t startPos = 0);
    static void removeDoubleQuotes(std::string& arg);
    static const char** getArgv(std::vector<std::string>& argsArray);
    static std::string assembleCommand(std::vector<std::string>& argsArray,
                                       std::string ignoreArg = "", int parametersRequired = 0);

    static std::string parse_exename(const char* arg);

private:
    void updateCommonArgs(const char* const* argv);
    bool checkUnexpectedArgs();

    static ArgsBase& argsBase() { return *m_argsBase; }

    bool parseMSWindowsArg(ArgsBase& argsBase, const int& argc, const char* const* argv, int& i);
    bool parseCarbonArg(ArgsBase& argsBase, const int& argc, const char* const* argv, int& i);
    bool parseXWindowsArg(ArgsBase& argsBase, const int& argc, const char* const* argv, int& i);

private:
    App* m_app;

    static ArgsBase* m_argsBase;
};
