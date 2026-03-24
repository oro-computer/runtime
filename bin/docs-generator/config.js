function stripInlineComment (source) {
  let result = ''
  let inSingle = false
  let inDouble = false
  let escape = false

  for (const char of source) {
    if (escape) {
      result += char
      escape = false
      continue
    }

    if (char === '\\' && (inSingle || inDouble)) {
      result += char
      escape = true
      continue
    }

    if (char === "'" && !inDouble) {
      inSingle = !inSingle
      result += char
      continue
    }

    if (char === '"' && !inSingle) {
      inDouble = !inDouble
      result += char
      continue
    }

    if (!inSingle && !inDouble && (char === ';' || char === '#')) {
      break
    }

    result += char
  }

  return result.trimEnd()
}

function firstAssignmentIndex (line) {
  let inSingle = false
  let inDouble = false
  let escape = false

  for (let i = 0; i < line.length; i++) {
    const char = line[i]

    if (escape) {
      escape = false
      continue
    }

    if (char === '\\' && (inSingle || inDouble)) {
      escape = true
      continue
    }

    if (char === "'" && !inDouble) {
      inSingle = !inSingle
      continue
    }

    if (char === '"' && !inSingle) {
      inDouble = !inDouble
      continue
    }

    if (!inSingle && !inDouble && (char === '=' || char === ':')) {
      return i
    }
  }

  return -1
}

function parseIni (iniText) {
  const sections = {}
  let currentSection = null
  let lastComment = ''
  let defaultValue = ''

  const appendComment = (text) => {
    const trimmed = text.trim()
    if (!trimmed) return
    lastComment = lastComment ? `${lastComment} ${trimmed}` : trimmed
  }

  iniText.split(/\r?\n/).forEach((line) => {
    const trimmedLine = line.trim()
    if (!trimmedLine) {
      lastComment = ''
      defaultValue = ''
      return
    }

    const isComment =
      (trimmedLine.startsWith(';') && !/;\s+\S+\s+=\s+.*/.test(trimmedLine)) ||
      trimmedLine.startsWith('#')

    if (isComment) {
      if (trimmedLine.includes('default value:')) {
        defaultValue = trimmedLine.split('default value:')[1].trim()
      } else {
        appendComment(trimmedLine.slice(1))
      }
      return
    }

    if (trimmedLine.startsWith('[') && trimmedLine.endsWith(']')) {
      currentSection = trimmedLine.slice(1, -1)
      sections[currentSection] = {
        description: lastComment,
        settings: []
      }
      lastComment = ''
      return
    }

    if (!currentSection) {
      return
    }

    const assignmentIndex = firstAssignmentIndex(trimmedLine)
    if (assignmentIndex === -1) {
      return
    }

    const key = trimmedLine.slice(0, assignmentIndex).trim()
    const valuePortion = trimmedLine.slice(assignmentIndex + 1)
    const rawValue = stripInlineComment(valuePortion).trim()

    if (!key) {
      return
    }

    sections[currentSection].settings.push({
      key,
      value: rawValue,
      defaultValue: defaultValue || rawValue,
      description: lastComment
    })

    lastComment = ''
    defaultValue = ''
  })

  return sections
}

function createConfigMd (sections) {
  let md = '# Configuration\n'
  md += '## Overview\n'
  md += `

The configuration file now lives in \`oro.toml\` (TOML) at the root of every Oro Runtime project.
The configuration file is read at compile time. Use \`.ororc\` in the project root for
developer-local overrides, simulator selections, and signing secrets that should not live in
version control.

Configuration precedence is:

1. \`oro.toml\`
2. \`.ororc\`
3. explicit CLI flags such as \`oroc build --platform=ios\`

The generated tables below document the scaffolded keys and starter values emitted by \`oroc init\`.
Values containing placeholders such as \`{{project_name}}\` are substituted when the project is created.
These are project-template values, not universal runtime defaults. For the live
runtime/CLI registry, use:

- \`oroc config --list\` to inspect all known keys and their effective values
- \`oroc config --describe <key>\` to inspect the description, default, and source for one key
- \`oroc config --format json\` to print the fully merged effective configuration

Example:

\`oro.toml\`:
\`\`\`toml
# other settings

[build]

headless = false

# other settings
\`\`\`

\`.ororc\`:
\`\`\`ini
[build]

platform = ios ; override the \`oroc build --platform\` CLI option


[settings.ios] ; override the \`[ios]\` section in \`oro.toml\`

codesign_identity = "iPhone Developer: John Doe (XXXXXXXXXX)"
distribution_method = "release-testing"
provisioning_profile = "johndoe.mobileprovision"
simulator_device = "iPhone 15"
\`\`\`

<tonic-toaster-inline
  title="Note"
  type="info">
    Note that "~" alias won't expand to the home directory in any of the configuration files.
    Use the full path instead.
</tonic-toaster-inline>
`
  md += '\n'
  Object.entries(sections).forEach(([sectionName, section]) => {
    const description = section?.description ?? ''
    const settings = Array.isArray(section)
      ? section
      : (section?.settings ?? [])
    md += `### \`${sectionName}\`\n`
    md += '\n'
    if (description) {
      md += `${description}\n\n`
    }
    md += '| Key | Scaffolded Value | Description |\n'
    md += '| :--- | :--- | :--- |\n'
    settings.forEach(({ key, defaultValue, description }) => {
      md += `| ${key} | ${defaultValue} | ${description} |\n`
    })
    md += '\n'
  })
  return md
}

// Generate config.md
export function generateConfig (source) {
  const startMarker = 'constexpr auto gDefaultConfig = R"INI('
  const endMarker = ')INI";'

  const startIndex = source.indexOf(startMarker)

  const remainingData = source.slice(startIndex + startMarker.length)
  const endIndex = remainingData.indexOf(endMarker)

  if (startIndex === -1 || endIndex === -1) {
    console.error('Start or end marker not found')
  }

  const extractedText = remainingData.slice(0, endIndex)

  const sections = parseIni(extractedText)
  return createConfigMd(sections)
}

export { parseIni, stripInlineComment, firstAssignmentIndex }
