#ifndef ORO_RUNTIME_SEMVER_H
#define ORO_RUNTIME_SEMVER_H

#include "platform.hh"
#include "string.hh"

namespace oro::runtime::semver {
  struct Version {
    int64_t major = 0;
    int64_t minor = 0;
    int64_t patch = 0;
    Vector<String> prerelease;
    Vector<String> build;

    String str () const;
  };

  enum class Compare {
    Less = -1,
    Equal = 0,
    Greater = 1
  };

  // Parse a SemVer 2.0.0 version string. Supports:
  // - Optional leading 'v' or '='
  // - Pre-release identifiers (e.g., 1.0.0-alpha.1)
  // - Build metadata (e.g., 1.0.0+build.1)
  // Returns true on success and writes a canonicalized Version to `out`.
  // On failure, returns false and, when non-null, writes a short error to `error`.
  bool parse (const String& input, Version& out, String* error = nullptr);

  // Compare two versions according to SemVer 2.0.0 precedence rules.
  Compare compare (const Version& a, const Version& b);

  inline bool eq (const Version& a, const Version& b) {
    return compare(a, b) == Compare::Equal;
  }

  inline bool lt (const Version& a, const Version& b) {
    return compare(a, b) == Compare::Less;
  }

  inline bool lte (const Version& a, const Version& b) {
    auto c = compare(a, b);
    return c == Compare::Less || c == Compare::Equal;
  }

  inline bool gt (const Version& a, const Version& b) {
    return compare(a, b) == Compare::Greater;
  }

  inline bool gte (const Version& a, const Version& b) {
    auto c = compare(a, b);
    return c == Compare::Greater || c == Compare::Equal;
  }

  // Release types roughly match npm's semver.inc:
  // - major, minor, patch
  // - premajor, preminor, prepatch, prerelease
  enum class ReleaseType {
    Major,
    Minor,
    Patch,
    Premajor,
    Preminor,
    Prepatch,
    Prerelease
  };

  // Increment a version in-place according to `type`. When `type` is one of
  // the pre* variants, `preid` (when non-empty) is used as the first
  // pre-release identifier (e.g., "beta" -> "1.2.3-beta.0").
  // Returns true on success, false when the input is invalid.
  bool inc (Version& version, ReleaseType type, const String& preid = "");

  // Convenience helpers for strings. These functions parse, operate, and
  // stringify using canonical SemVer rules.
  bool valid (const String& input);
  String clean (const String& input); // returns empty string when invalid
  Compare compare (const String& a, const String& b, bool* ok = nullptr);

  inline bool eq (const String& a, const String& b) {
    return compare(a, b) == Compare::Equal;
  }

  inline bool lt (const String& a, const String& b) {
    return compare(a, b) == Compare::Less;
  }

  inline bool lte (const String& a, const String& b) {
    auto c = compare(a, b);
    return c == Compare::Less || c == Compare::Equal;
  }

  inline bool gt (const String& a, const String& b) {
    return compare(a, b) == Compare::Greater;
  }

  inline bool gte (const String& a, const String& b) {
    auto c = compare(a, b);
    return c == Compare::Greater || c == Compare::Equal;
  }

  // Increment helper that operates directly on a version string. Returns an
  // empty string on failure.
  String inc (const String& version, ReleaseType type, const String& preid = "");

  // Range support: a minimal but robust implementation of SemVer range
  // semantics. The grammar supported is:
  //
  //   range         ::= range_set ('||' range_set)*
  //   range_set     ::= comparator+
  //   comparator    ::= (op? version)
  //   op            ::= '<' | '<=' | '>' | '>=' | '='
  //
  // Version strings within ranges use full SemVer (including pre-release).
  // Wildcards, ^, ~, and hyphen ranges are intentionally not supported yet.

  enum class Operator {
    LT,
    LTE,
    GT,
    GTE,
    EQ
  };

  struct Comparator {
    Operator op = Operator::EQ;
    Version version;
  };

  struct RangeSet {
    Vector<Comparator> comparators;
  };

  struct Range {
    Vector<RangeSet> sets;
  };

  bool parseRange (const String& input, Range& out, String* error = nullptr);
  bool satisfies (const Version& version, const Range& range);
  bool satisfies (const String& version, const String& range, String* error = nullptr);
}

#endif

