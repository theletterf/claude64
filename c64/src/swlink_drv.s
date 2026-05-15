; Embeds the cc65 Swiftlink serial driver so the PRG is self-contained.
; C side: extern unsigned char swlink_drv[];  ser_install(swlink_drv);

        .export _swlink_drv

        .segment "RODATA"

_swlink_drv:
        .incbin "/opt/homebrew/share/cc65/target/c64/drv/ser/c64-swlink.ser"
