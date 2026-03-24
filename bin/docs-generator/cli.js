function toKebabCase (inputString) {
  return inputString.replace(/([a-z])([A-Z])/g, '$1-$2').toLowerCase()
}

function extractSectionSources (source) {
  const startMarker = /constexpr auto gHelpText(\S*) = R"TEXT\(/gm
  const endMarker = ')TEXT";'

  return [...source.matchAll(startMarker)].map((match) => {
    const startIndex = match.index + match[0].length
    const remainingData = source.slice(startIndex)
    const endIndex = remainingData.indexOf(endMarker)
    const extractedText = remainingData.slice(0, endIndex)
    return { section: toKebabCase(match[1]), extractedText }
  })
}

function parseIni (iniText) {
  const sections = {}
  const section = iniText.section === '' ? 'oroc' : `oroc ${iniText.section}`

  sections[section] = {}

  let currentSubSection
  const description = []
  let option

  for (const line of iniText.extractedText.split('\n')) {
    const trimmedLine = line.trim()
    const isSubSection = /\w+:$/.test(trimmedLine) && trimmedLine[0] !== '-'

    if (trimmedLine.length === 0) {
      continue
    }

    if (/^oroc v[0-9]/.test(trimmedLine)) {
      continue
    }

    if (line[0] !== ' ' && !isSubSection) {
      description.push(trimmedLine)
      continue
    }

    if (isSubSection) {
      currentSubSection = trimmedLine.replace(/:$/, '')
      sections[section][currentSubSection] =
        currentSubSection.includes('options') ||
        currentSubSection === 'subcommands'
          ? {}
          : []
      option = undefined
      continue
    }

    if (!currentSubSection) {
      description.push(trimmedLine)
      continue
    }

    if (
      !currentSubSection.includes('options') &&
      currentSubSection !== 'subcommands'
    ) {
      sections[section][currentSubSection].push(trimmedLine)
      continue
    }

    if (/.+\s{2,}.+/.test(trimmedLine)) {
      const [value, desc] = trimmedLine.split(/\s{2,}/)
      option = value
      sections[section][currentSubSection][value] = [desc]
      continue
    }

    if (option) {
      sections[section][currentSubSection][option].push(trimmedLine)
    }
  }

  sections[section].description = description
  return sections
}

function generateIni (parts) {
  return parts.map(parseIni)
}

function parseCliSections (source) {
  return generateIni(extractSectionSources(source))
}

function getSectionEntries (sections) {
  return sections.flatMap((section) =>
    Object.entries(section).map(([title, content]) => ({ title, content }))
  )
}

function createCliMd (sections) {
  let md = '# Command Line interface\n'
  md +=
    'These commands are available from the command line interface (CLI).\n\n'

  getSectionEntries(sections).forEach(({ title, content }) => {
    md += `## ${title}\n`
    const { description, usage, ...subsections } = content
    if (description?.length) {
      md += description.join('\n') + '\n\n'
    }
    if (usage?.length) {
      md += '### Usage\n'
      md += '```bash\n'
      md += usage.join('\n') + '\n'
      md += '```\n\n'
    }

    Object.entries(subsections).forEach(([subKey, subVal]) => {
      if (Array.isArray(subVal)) {
        if (subVal.length) {
          md += `### ${subKey}\n`
          md += subVal.join('\n') + '\n\n'
        }
        return
      }

      md += `### ${subKey}\n`
      md += '| Option | Description |\n'
      md += '| --- | --- |\n'
      Object.entries(subVal).forEach(([opt, desc]) => {
        const lines = Array.isArray(desc) ? desc : [String(desc)]
        md += `| ${opt} | ${lines.join('<br>')} |\n`
      })
      md += '\n'
    })
  })

  return md
}

function escapeRoffText (text) {
  let value = String(text).replace(/\\/g, '\\\\').replace(/-/g, '\\-')

  if (value.startsWith('.') || value.startsWith("'")) {
    value = `\\&${value}`
  }

  return value
}

function foldWrappedLines (lines) {
  const entries = []
  let current = []

  for (const line of lines) {
    const trimmed = line.trim()
    if (!trimmed) {
      if (current.length > 0) {
        entries.push(current.join(' '))
        current = []
      }
      continue
    }

    if (trimmed.startsWith('- ')) {
      if (current.length > 0) {
        entries.push(current.join(' '))
        current = []
      }
      entries.push(trimmed)
      continue
    }

    current.push(trimmed)
  }

  if (current.length > 0) {
    entries.push(current.join(' '))
  }

  return entries
}

function renderRoffParagraphs (lines) {
  let output = ''

  foldWrappedLines(lines).forEach((entry) => {
    if (entry.startsWith('- ')) {
      output += '.IP \\(bu 2\n'
      output += `${escapeRoffText(entry.slice(2))}\n`
      return
    }

    output += '.PP\n'
    output += `${escapeRoffText(entry)}\n`
  })

  return output
}

function isDefinitionList (lines) {
  const entries = lines.filter((line) => line.trim().length > 0)
  return (
    entries.length > 0 &&
    entries.every((line) => /.+\s{2,}.+/.test(line.trim()))
  )
}

function renderRoffDefinitionList (lines) {
  let output = ''

  lines
    .filter((line) => line.trim().length > 0)
    .forEach((line) => {
      const [label, description] = line.trim().split(/\s{2,}/)
      output += '.TP\n'
      output += `\\fB${escapeRoffText(label)}\\fR\n`
      output += `${escapeRoffText(description)}\n`
    })

  return output
}

function renderRoffOptionDescription (lines) {
  const entries = foldWrappedLines(lines)
  if (entries.length === 0) {
    return `${escapeRoffText('')}\n`
  }

  return (
    entries
      .map((entry, index) => {
        let text = entry
        if (entry.startsWith('- ')) {
          text = `\\(bu ${escapeRoffText(entry.slice(2))}`
        } else {
          text = escapeRoffText(entry)
        }

        return `${index > 0 ? '.br\n' : ''}${text}`
      })
      .join('\n') + '\n'
  )
}

function renderRoffExamples (lines) {
  let output = ''
  let currentCommand = ''
  let currentDescription = []

  const flushCurrent = () => {
    if (!currentCommand) {
      return
    }

    output += '.TP\n'
    output += `\\fB${escapeRoffText(currentCommand)}\\fR\n`
    if (currentDescription.length > 0) {
      output += `${escapeRoffText(currentDescription.join(' '))}\n`
    }

    currentCommand = ''
    currentDescription = []
  }

  for (const line of lines) {
    const trimmed = line.trim()
    if (trimmed.startsWith('oroc ')) {
      flushCurrent()
      currentCommand = trimmed
      continue
    }

    if (trimmed.length > 0) {
      currentDescription.push(trimmed)
    }
  }

  flushCurrent()
  return output
}

function toManSectionTitle (title) {
  return title.toUpperCase()
}

function deriveCommandTokens (title, usageLines) {
  const firstUsage = usageLines?.find(Boolean) ?? title
  const tokens = firstUsage.trim().split(/\s+/)
  const commandTokens = []

  for (const token of tokens) {
    if (commandTokens.length === 0 && token !== 'oroc') {
      continue
    }

    if (/^(?:\[|<|-)/.test(token)) {
      break
    }

    commandTokens.push(token)
  }

  if (commandTokens.length > 0) {
    return commandTokens
  }

  return title.trim().split(/\s+/)
}

function deriveSummary (commandTokens, description) {
  if (description?.length) {
    return description[0]
  }

  if (commandTokens.length === 1 && commandTokens[0] === 'oroc') {
    return 'command line interface for Oro Runtime projects'
  }

  return 'Oro Runtime command'
}

function deriveSeeAlso (commandTokens) {
  const selfPage = `${commandTokens.join('-')}(1)`
  const pages = new Set()

  if (commandTokens.join(' ') !== 'oroc') {
    pages.add('oroc(1)')
  }

  if (commandTokens.length > 2) {
    pages.add(`${commandTokens.slice(0, -1).join('-')}(1)`)
  }

  pages.delete(selfPage)
  return [...pages]
}

function createCliManPage ({ title, content, cliVersion }) {
  const { description = [], usage = [], ...subsections } = content
  const commandTokens = deriveCommandTokens(title, usage)
  const pageName = commandTokens.join('-')
  const summary = deriveSummary(commandTokens, description)
  const seeAlso = deriveSeeAlso(commandTokens)

  let man = `.TH ${pageName.toUpperCase()} 1 "" "Oro Runtime ${escapeRoffText(cliVersion)}" "Oro Runtime Manual"\n`
  man += '.SH NAME\n'
  man += `${escapeRoffText(pageName)} \\- ${escapeRoffText(summary)}\n`

  if (usage.length > 0) {
    man += '.SH SYNOPSIS\n'
    man += '.nf\n'
    man += usage.map(escapeRoffText).join('\n') + '\n'
    man += '.fi\n'
  }

  if (description.length > 0 || pageName === 'oroc') {
    man += '.SH DESCRIPTION\n'
    if (description.length > 0) {
      man += renderRoffParagraphs(description)
    } else {
      man += '.PP\n'
      man += `${escapeRoffText('oroc is the main command line interface for Oro Runtime projects.')}\n`
    }
  }

  Object.entries(subsections).forEach(([subKey, subVal]) => {
    man += `.SH ${toManSectionTitle(subKey)}\n`

    if (Array.isArray(subVal)) {
      if (subKey === 'examples') {
        man += renderRoffExamples(subVal)
      } else if (isDefinitionList(subVal)) {
        man += renderRoffDefinitionList(subVal)
      } else {
        man += renderRoffParagraphs(subVal)
      }
      return
    }

    Object.entries(subVal).forEach(([optionName, lines]) => {
      const descriptionLines = Array.isArray(lines) ? lines : [String(lines)]
      man += '.TP\n'
      man += `\\fB${escapeRoffText(optionName)}\\fR\n`
      man += renderRoffOptionDescription(descriptionLines)
    })
  })

  if (seeAlso.length > 0) {
    man += '.SH SEE ALSO\n'
    man += `${seeAlso.map(escapeRoffText).join(', ')}\n`
  }

  return {
    filename: `${pageName}.1`,
    content: man
  }
}

export function generateCli (source) {
  return createCliMd(parseCliSections(source))
}

export function generateCliManpages (source, { cliVersion }) {
  return getSectionEntries(parseCliSections(source))
    .map((section) => createCliManPage({ ...section, cliVersion }))
    .sort((a, b) => a.filename.localeCompare(b.filename))
}
