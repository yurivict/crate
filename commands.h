#pragma once

class Args;
class Spec;

bool createCrate(const Args &args, const Spec &spec);
bool runCrate(const Args &args, int argc, char** argv, int &outReturnCode);
