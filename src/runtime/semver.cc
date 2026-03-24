#include "semver.hh"

#include <algorithm>
#include <cctype>
#include <limits>

using oro::runtime::string::split;
using oro::runtime::string::trim;

namespace oro::runtime::semver {
  namespace {
    bool isDigit (char c) {
      return c >= '0' && c <= '9';
    }

    bool isAlphaNum (char c) {
      return (c >= 'A' && c <= 'Z') ||
             (c >= 'a' && c <= 'z') ||
             isDigit(c);
    }

    bool parseNumericIdentifier (const String& input, int64_t& out) {
      if (input.empty()) {
        return false;
      }

      if (input.size() > 1 && input[0] == '0') {
        return false;
      }

      int64_t value = 0;
      for (char c : input) {
        if (!isDigit(c)) {
          return false;
        }
        const int digit = c - '0';
        if (value > (std::numeric_limits<int64_t>::max() - digit) / 10) {
          return false;
        }
        value = (value * 10) + digit;
      }

      out = value;
      return true;
    }

    bool validateIdentifier (const String& id, bool numericAllowed, bool strictNumeric, String* error) {
      if (id.empty()) {
        if (error) *error = "empty identifier";
        return false;
      }

      bool allDigits = true;
      for (char c : id) {
        if (!(isAlphaNum(c) || c == '-')) {
          if (error) *error = "identifier contains invalid characters";
          return false;
        }
        if (!isDigit(c)) {
          allDigits = false;
        }
      }

      if (!numericAllowed && allDigits) {
        if (error) *error = "numeric identifier not allowed in this context";
        return false;
      }

      if (strictNumeric && allDigits && id.size() > 1 && id[0] == '0') {
        if (error) *error = "numeric identifier must not contain leading zeros";
        return false;
      }

      return true;
    }

    Vector<String> splitDot (const String& input) {
      return split(input, '.');
    }
  }

  String Version::str () const {
    StringStream ss;
    ss << major << "." << minor << "." << patch;
    if (!prerelease.empty()) {
      ss << "-";
      for (size_t i = 0; i < prerelease.size(); i++) {
        if (i > 0) ss << ".";
        ss << prerelease[i];
      }
    }
    if (!build.empty()) {
      ss << "+";
      for (size_t i = 0; i < build.size(); i++) {
        if (i > 0) ss << ".";
        ss << build[i];
      }
    }
    return ss.str();
  }

  bool parse (const String& raw, Version& out, String* error) {
    String input = trim(raw);
    if (input.empty()) {
      if (error) *error = "empty version string";
      return false;
    }

    if (!input.empty() && (input[0] == 'v' || input[0] == 'V')) {
      input = input.substr(1);
    }

    if (!input.empty() && input[0] == '=') {
      input = input.substr(1);
    }

    String buildPart;
    auto plusPos = input.find('+');
    if (plusPos != String::npos) {
      buildPart = input.substr(plusPos + 1);
      input = input.substr(0, plusPos);
    }

    String prePart;
    auto hyphenPos = input.find('-');
    if (hyphenPos != String::npos) {
      prePart = input.substr(hyphenPos + 1);
      input = input.substr(0, hyphenPos);
    }

    auto firstDot = input.find('.');
    auto secondDot = input.find('.', firstDot == String::npos ? String::npos : firstDot + 1);
    if (firstDot == String::npos || secondDot == String::npos) {
      if (error) *error = "version must be in 'major.minor.patch' form";
      return false;
    }

    const String majorStr = input.substr(0, firstDot);
    const String minorStr = input.substr(firstDot + 1, secondDot - firstDot - 1);
    const String patchStr = input.substr(secondDot + 1);

    Version result;
    if (!parseNumericIdentifier(majorStr, result.major)) {
      if (error) *error = "invalid major version";
      return false;
    }
    if (!parseNumericIdentifier(minorStr, result.minor)) {
      if (error) *error = "invalid minor version";
      return false;
    }
    if (!parseNumericIdentifier(patchStr, result.patch)) {
      if (error) *error = "invalid patch version";
      return false;
    }

    if (!prePart.empty()) {
      const auto identifiers = splitDot(prePart);
      for (const auto& id : identifiers) {
        String idError;
        if (!validateIdentifier(id, true, true, &idError)) {
          if (error) *error = "invalid pre-release identifier: " + idError;
          return false;
        }
        result.prerelease.push_back(id);
      }
    }

    if (!buildPart.empty()) {
      const auto identifiers = splitDot(buildPart);
      for (const auto& id : identifiers) {
        String idError;
        if (!validateIdentifier(id, true, false, &idError)) {
          if (error) *error = "invalid build metadata identifier: " + idError;
          return false;
        }
        result.build.push_back(id);
      }
    }

    out = result;
    return true;
  }

  Compare compare (const Version& a, const Version& b) {
    if (a.major != b.major) {
      return a.major < b.major ? Compare::Less : Compare::Greater;
    }
    if (a.minor != b.minor) {
      return a.minor < b.minor ? Compare::Less : Compare::Greater;
    }
    if (a.patch != b.patch) {
      return a.patch < b.patch ? Compare::Less : Compare::Greater;
    }

    const bool aHasPre = !a.prerelease.empty();
    const bool bHasPre = !b.prerelease.empty();
    if (!aHasPre && !bHasPre) {
      return Compare::Equal;
    }
    if (!aHasPre && bHasPre) {
      return Compare::Greater;
    }
    if (aHasPre && !bHasPre) {
      return Compare::Less;
    }

    const auto maxLen = std::max(a.prerelease.size(), b.prerelease.size());
    for (size_t i = 0; i < maxLen; i++) {
      const bool aHas = i < a.prerelease.size();
      const bool bHas = i < b.prerelease.size();
      if (!aHas && bHas) {
        return Compare::Less;
      }
      if (aHas && !bHas) {
        return Compare::Greater;
      }

      const String& ai = a.prerelease[i];
      const String& bi = b.prerelease[i];

      const bool aNumeric = !ai.empty() && std::all_of(ai.begin(), ai.end(), isDigit);
      const bool bNumeric = !bi.empty() && std::all_of(bi.begin(), bi.end(), isDigit);

      if (aNumeric && bNumeric) {
        int64_t av = 0;
        int64_t bv = 0;
        parseNumericIdentifier(ai, av);
        parseNumericIdentifier(bi, bv);
        if (av < bv) {
          return Compare::Less;
        }
        if (av > bv) {
          return Compare::Greater;
        }
        continue;
      }

      if (aNumeric && !bNumeric) {
        return Compare::Less;
      }
      if (!aNumeric && bNumeric) {
        return Compare::Greater;
      }

      if (ai < bi) {
        return Compare::Less;
      }
      if (ai > bi) {
        return Compare::Greater;
      }
    }

    return Compare::Equal;
  }

  bool inc (Version& version, ReleaseType type, const String& preid) {
    auto clearPrerelease = [&version]() {
      version.prerelease.clear();
      version.build.clear();
    };

    auto initPrerelease = [&version, &preid]() {
      version.prerelease.clear();
      version.build.clear();
      if (!preid.empty()) {
        version.prerelease.push_back(preid);
        version.prerelease.push_back("0");
      } else {
        version.prerelease.push_back("rc");
        version.prerelease.push_back("0");
      }
    };

    auto bumpNumericTail = [&version]() {
      if (version.prerelease.empty()) {
        return false;
      }
      for (int i = static_cast<int>(version.prerelease.size()) - 1; i >= 0; --i) {
        String& id = version.prerelease[static_cast<size_t>(i)];
        bool allDigits = !id.empty() && std::all_of(id.begin(), id.end(), isDigit);
        if (!allDigits) {
          continue;
        }
        int64_t value = 0;
        if (!parseNumericIdentifier(id, value)) {
          continue;
        }
        value++;
        version.prerelease[static_cast<size_t>(i)] = std::to_string(value);
        return true;
      }
      version.prerelease.push_back("0");
      return true;
    };

    switch (type) {
      case ReleaseType::Major: {
        if (version.major == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.major++;
        version.minor = 0;
        version.patch = 0;
        clearPrerelease();
        break;
      }
      case ReleaseType::Minor: {
        if (version.minor == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.minor++;
        version.patch = 0;
        clearPrerelease();
        break;
      }
      case ReleaseType::Patch: {
        if (version.patch == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.patch++;
        clearPrerelease();
        break;
      }
      case ReleaseType::Premajor: {
        if (version.major == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.major++;
        version.minor = 0;
        version.patch = 0;
        initPrerelease();
        break;
      }
      case ReleaseType::Preminor: {
        if (version.minor == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.minor++;
        version.patch = 0;
        initPrerelease();
        break;
      }
      case ReleaseType::Prepatch: {
        if (version.patch == std::numeric_limits<int64_t>::max()) {
          return false;
        }
        version.patch++;
        initPrerelease();
        break;
      }
      case ReleaseType::Prerelease: {
        if (version.prerelease.empty()) {
          if (version.patch == std::numeric_limits<int64_t>::max()) {
            return false;
          }
          version.patch++;
          initPrerelease();
        } else {
          bumpNumericTail();
        }
        break;
      }
      default:
        return false;
    }

    return true;
  }

  bool valid (const String& input) {
    Version v;
    return parse(input, v, nullptr);
  }

  String clean (const String& input) {
    Version v;
    if (!parse(input, v, nullptr)) {
      return "";
    }
    return v.str();
  }

  Compare compare (const String& a, const String& b, bool* ok) {
    Version va;
    Version vb;
    String err;
    if (!parse(a, va, &err)) {
      if (ok) *ok = false;
      return Compare::Equal;
    }
    if (!parse(b, vb, &err)) {
      if (ok) *ok = false;
      return Compare::Equal;
    }
    if (ok) *ok = true;
    return compare(va, vb);
  }

  String inc (const String& version, ReleaseType type, const String& preid) {
    Version v;
    if (!parse(version, v, nullptr)) {
      return "";
    }
    if (!inc(v, type, preid)) {
      return "";
    }
    return v.str();
  }

  namespace {
    bool parseOperator (const String& token, size_t& pos, Operator& op) {
      if (pos >= token.size()) {
        return false;
      }

      if (token[pos] == '<' || token[pos] == '>') {
        bool isLT = token[pos] == '<';
        pos++;
        if (pos < token.size() && token[pos] == '=') {
          pos++;
          op = isLT ? Operator::LTE : Operator::GTE;
        } else {
          op = isLT ? Operator::LT : Operator::GT;
        }
        return true;
      }

      if (token[pos] == '=') {
        op = Operator::EQ;
        pos++;
        return true;
      }

      op = Operator::EQ;
      return true;
    }
  }

  bool parseRange (const String& input, Range& out, String* error) {
    Range range;
    auto orSegments = split(input, '|');
    Vector<String> normalizedSegments;

    if (orSegments.size() == 1) {
      normalizedSegments.push_back(trim(orSegments[0]));
    } else {
      String current;
      for (size_t i = 0; i < orSegments.size(); i++) {
        auto token = orSegments[i];
        if (token == "") {
          continue;
        }

        if (!current.empty()) {
          current += "||";
        }
        current += token;
      }
      normalizedSegments.push_back(trim(current));
    }

    const auto orSplit = split(input, "||");
    for (const auto& rawSet : orSplit) {
      const auto setStr = trim(rawSet);
      if (setStr.empty()) {
        continue;
      }

      RangeSet set;
      const auto tokens = split(setStr, ' ');
      for (const auto& rawToken : tokens) {
        const auto token = trim(rawToken);
        if (token.empty()) {
          continue;
        }

        size_t pos = 0;
        Operator op;
        if (!parseOperator(token, pos, op)) {
          if (error) *error = "invalid comparator operator in range";
          return false;
        }

        const auto verStr = trim(token.substr(pos));
        if (verStr.empty()) {
          if (error) *error = "missing version in range comparator";
          return false;
        }

        Version v;
        String parseError;
        if (!parse(verStr, v, &parseError)) {
          if (error) *error = "invalid version in range: " + parseError;
          return false;
        }

        Comparator cmp;
        cmp.op = op;
        cmp.version = v;
        set.comparators.push_back(cmp);
      }

      if (!set.comparators.empty()) {
        range.sets.push_back(set);
      }
    }

    if (range.sets.empty()) {
      if (error) *error = "empty range";
      return false;
    }

    out = range;
    return true;
  }

  bool satisfies (const Version& version, const Range& range) {
    for (const auto& set : range.sets) {
      bool allSatisfied = true;
      for (const auto& cmp : set.comparators) {
        auto c = compare(version, cmp.version);
        bool ok = false;
        switch (cmp.op) {
          case Operator::EQ:
            ok = (c == Compare::Equal);
            break;
          case Operator::LT:
            ok = (c == Compare::Less);
            break;
          case Operator::LTE:
            ok = (c == Compare::Less || c == Compare::Equal);
            break;
          case Operator::GT:
            ok = (c == Compare::Greater);
            break;
          case Operator::GTE:
            ok = (c == Compare::Greater || c == Compare::Equal);
            break;
        }
        if (!ok) {
          allSatisfied = false;
          break;
        }
      }

      if (allSatisfied) {
        return true;
      }
    }

    return false;
  }

  bool satisfies (const String& versionStr, const String& rangeStr, String* error) {
    Version v;
    if (!parse(versionStr, v, error)) {
      return false;
    }
    Range r;
    if (!parseRange(rangeStr, r, error)) {
      return false;
    }
    return satisfies(v, r);
  }
}
