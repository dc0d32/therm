#include "utils.h"
#include <ctype.h>
#include <Esp.h>

void trim_string(String &str)
{
    int len = str.length();
    for (int idx = len - 1; idx >= 0; idx--)
    {
        char ch = str.charAt(idx);
        if (isspace(ch) || isblank(ch) || ch == '\n' || ch == '\r')
        {
            len = idx;
            continue;
        }
        break;
    }
    str.remove(len);
}

String get_chip_id()
{
    return String(ESP.getChipId());
}
