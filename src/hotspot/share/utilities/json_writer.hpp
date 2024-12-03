#include "utilities/ostream.hpp"
#include <cstddef>

class JsonArray;

class JsonInt {
  int number;
public:
  JsonInt(int number): number(number) {}

  void write(outputStream& stream) {
    stream.print("%d", number);
  }
};

class JsonString {
  const char* string;
public:
  JsonString(const char* string): string(string) {}

  void write(outputStream& stream) {
    stream.print("\"%s\"", string);
  }
};

class JsonBool {
  bool boolean;
public:
  JsonBool(bool boolean): boolean(boolean) {}

  void write(outputStream& stream) {
    if (boolean) stream.print("true");
    else stream.print("false");
  }
};

class JsonNull {
public:
  JsonNull() {}

  void write(outputStream& stream) {
    stream.print("null");
  }
  static JsonNull null;
};


class JsonObject : public StackObj {
  stringStream _stream;
  int _count; // For keeping tracking of commas
  bool _has_ended;

  template<typename J>
  void internal_put(const char* name, J& j) {
    assert(!_has_ended, "");
    if (_count > 0) {
      _stream.print(",\n");
    }
    JsonString str(name);
    str.write(_stream);
    _stream.print(":");
    j.write(_stream);
    _count++;
  }

public:
  JsonObject() : _stream(), _count(0), _has_ended(false) {
    _stream.print("{\n");
  }
  void end() {
    _has_ended = true;
    _stream.print("}");
  }
  void write(outputStream& stream) {
    assert(_has_ended, "must be");
    stream.print("%s", _stream.freeze());
  }

  void put(const char* name, const char* a) { JsonString x(a); internal_put(name, x); }
  void put(const char* name, int a) { JsonInt x(a); internal_put(name, x); }
  void put(const char* name, bool a) { JsonBool x(a); internal_put(name, x); }
  void put(const char* name, JsonNull a) { internal_put(name, a); }
  void put(const char* name, JsonObject& o) {internal_put(name, o); }
  void put(const char* name, JsonArray& a) {internal_put(name, a); }
};

class JsonArray : public StackObj {
  stringStream stream;
  int count; // For keeping tracking of commas
  bool has_ended;

  template<typename J>
  void internal_put(J& j) {
    assert(!has_ended, "");
    if (count > 0) {
      stream.print(",\n");
    }
    j.write(stream);
    count++;
  }

public:
  JsonArray()
    : stream(),
      count(0),
      has_ended(false) {
    stream.print("[\n");
  }

  void end() {
    has_ended = true;
    stream.print("]");
  }

  void write(outputStream& stream) {
    assert(has_ended, "must be");
    stream.print("%s", this->stream.freeze());
  }

  void put(const char* name, const char* a) { JsonString x(a); internal_put(x); }
  void put(int a) { JsonInt x(a); internal_put(x); }
  void put(JsonBool a) { JsonBool x(a); internal_put(x); }
  void put(JsonNull a) { internal_put(a); }
  void put(JsonObject& o) { internal_put(o); }
  void put(JsonArray& a) { internal_put(a); }
};
