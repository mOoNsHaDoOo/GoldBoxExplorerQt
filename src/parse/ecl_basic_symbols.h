#pragma once

// Variable names used by the ECL Basic listing (Champions of Krynn, Amiga).
// key is "R:XXXX": region (A, B, C or R) and the address the operand points
// to after removing the $4B00 variable offset.
struct EclBasicSymbol
{
    const char *key;
    const char *name;
    const char *description;
};

const EclBasicSymbol *eclBasicSymbols(int *count);
