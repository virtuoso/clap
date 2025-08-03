# Generate output markdown doc

file(GLOB tocs "${CLAP_DOC_API_DIR}/${dir}/*.toc.md")
file(GLOB bodies "${CLAP_DOC_API_DIR}/${dir}/*.body.md")

set(result "# Clap ${dir} API reference\n")

# Collect table of contents entries
foreach(toc ${tocs})
    file(READ "${toc}" toc_entry)

    cmake_path(RELATIVE_PATH toc
        BASE_DIRECTORY ${CLAP_DOC_API_DIR}
        OUTPUT_VARIABLE toc_title
    )
    string(REPLACE ".toc.md" "" toc_title "${toc_title}")

    set(result "${result}\n## ${toc_title}\n")
    set(result "${result}${toc_entry}")
endforeach()

set(result "${result}\n---\n")

# Collect documentation bodies
foreach(body ${bodies})
    file(READ "${body}" body_entry)
    set(result "${result}${body_entry}")
endforeach()

file(WRITE "${output}" "${result}")
