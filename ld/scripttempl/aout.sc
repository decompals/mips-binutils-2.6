cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

${RELOCATING+${LIB_SEARCH_DIRS}}
${STACKZERO+${RELOCATING+${STACKZERO}}}
${SHLIB_PATH+${RELOCATING+${SHLIB_PATH}}}
${RELOCATING+${EXECUTABLE_SYMBOLS}}
SECTIONS
{
  ${RELOCATING+. = ${TEXT_START_ADDR};}
  .text :
  {
    CREATE_OBJECT_SYMBOLS
    *(.text)
    /* The next six sections are for SunOS dynamic linking.  The order
       is important.  */
    *(.dynrel)
    *(.hash)
    *(.dynsym)
    *(.dynstr)
    *(.rules)
    *(.need)
    ${RELOCATING+_etext = .;}
    ${RELOCATING+__etext = .;}
    ${PAD_TEXT+${RELOCATING+. = ${DATA_ALIGNMENT};}}
  }
  ${RELOCATING+. = ${DATA_ALIGNMENT};}
  .data :
  {
    /* The first three sections are for SunOS dynamic linking.  */
    *(.dynamic)
    *(.got)
    *(.plt)
    *(.data)
    *(.linux-dynamic) /* For Linux dynamic linking.  */
    ${CONSTRUCTING+CONSTRUCTORS}
    ${RELOCATING+_edata  =  .;}
    ${RELOCATING+__edata  =  .;}
  }
  .bss :
  {
   ${RELOCATING+ __bss_start = .};
   *(.bss)
   *(COMMON)
   ${RELOCATING+_end = ALIGN(4) };
   ${RELOCATING+__end = ALIGN(4) };
  }
}
EOF
