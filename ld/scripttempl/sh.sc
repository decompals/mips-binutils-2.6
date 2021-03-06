cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

MEMORY
{
  ram : o = 0x1000, l = 512k
}

SECTIONS
{
  .text :
  {
    *(.text)
    *(.strings)
    ${RELOCATING+ _etext = . ; }
  } ${RELOCATING+ > ram}
  .tors :
  {
    ___ctors = . ;
    *(.ctors)
    ___ctors_end = . ;
    ___dtors = . ;
    *(.dtors)
    ___dtors_end = . ;
  } ${RELOCATING+ > ram}
  .data :
  {
    *(.data)
    ${RELOCATING+ _edata = . ; }
  } ${RELOCATING+ > ram}
  .bss :
  {
    ${RELOCATING+ _bss_start = . ; }
    *(.bss)
    *(COMMON)
    ${RELOCATING+ _end = . ;  }
  } ${RELOCATING+ > ram}
  .stack ${RELOCATING+ 0x30000 }  :
  {
    ${RELOCATING+ _stack = . ; }
    *(.stack)
  } ${RELOCATING+ > ram}
  .stab 0 ${RELOCATING+(NOLOAD)} :
  {
    *(.stab)
  }
  .stabstr 0 ${RELOCATING+(NOLOAD)} :
  {
    *(.stabstr)
  }
}
EOF




