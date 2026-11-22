#pragma once

#include <QString>

// Working folder of the running program (disk images are extracted here):
// "tmp" in the folder of the executable, so nothing depends on the host
// system.  It is deleted and created again when the program starts, and
// deleted when it ends.  When the program folder is not writable the
// system temporary folder is used instead (GoldBoxExplorerQt-tmp).
namespace TempFolder {

QString path();

// Deletes any leftover folder and creates an empty one.
bool reset();

// Deletes the folder.
void remove();

// Creates the folder at construction and deletes it at destruction.
class Scope
{
public:
    Scope() { reset(); }
    ~Scope() { remove(); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

} // namespace TempFolder
