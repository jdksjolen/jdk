#include "precompiled.hpp"
#include "jvm.h"
#include "memory/resourceArea.hpp"
#include "utilities/json_writer.hpp"
#include "unittest.hpp"
#include "utilities/ostream.hpp"

TEST(TestWriter, Write) {
  JsonObject o;
  o.put("first", false);
  o.put("second", 55);
  JsonArray a;
  for(int i = 0; i < 10; i++) {
    a.put(i);
  }
  a.end();
  o.put("third", a);
  o.end();
  o.write(*tty);
}
