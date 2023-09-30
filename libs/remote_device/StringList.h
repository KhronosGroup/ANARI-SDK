
#pragma once

#include <cstring>
#include <vector>

namespace remote {

// RAII List type compatible with the const char **
// semantics of ANARI string lists
class StringList
{
 public:
  StringList() = default;

  // initialize from list of const char *
  // last pointer must be NULL!
  StringList(const char **strings)
  {
    if (strings) {
      while (auto c_str = *strings++) {
        std::string str(c_str);
        this->push_back(str);
      }
      value.push_back(nullptr);
    }
  }

  StringList(const StringList &other)
  {
    for (size_t i = 0; i < other.value.size(); ++i) {
      if (other.value[i] == nullptr)
        value.push_back(nullptr);
      else {
        std::string str(other.value[i]);
        this->push_back(str);
      }
    }
  }

  StringList(StringList &&other)
  {
    value = std::move(other.value);
    other.value.clear();
  }

  ~StringList()
  {
    for (size_t i = 0; i < value.size(); ++i) {
      delete[] value[i];
    }
  }

  StringList &operator=(const StringList &other)
  {
    if (&other != this) {
      for (size_t i = 0; i < other.value.size(); ++i) {
        if (other.value[i] == nullptr)
          value.push_back(nullptr);
        else {
          std::string str(other.value[i]);
          this->push_back(str);
        }
      }
    }
    return *this;
  }

  StringList &operator=(StringList &&other)
  {
    if (&other != this) {
      value = std::move(other.value);
    }
    return *this;
  }

  void push_back(std::string str)
  {
    if (str.length() > 0) {
      char *val = new char[str.length() + 1];
      memcpy(val, str.data(), str.length());
      val[str.length()] = '\0';
      value.push_back(val);
    } else {
      value.push_back(nullptr);
    }
  }

  const char **data() const
  {
    return (const char **)value.data();
  }

  size_t size() const
  {
    return value.size();
  }

 private:
  std::vector<char *> value;
};

} // namespace remote
