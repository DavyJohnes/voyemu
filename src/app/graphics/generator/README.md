# Graphics dispatch regeneration

This optional maintenance generator recreates the checked-in dispatch with JDK 25 or newer. The build and the Voyemu runtime consume the generated C++ directly. `--release 25` matches the class-file level of the CTest and Java helper builds.

Create `.local/build/jobs/graphics-generator` and run these commands from the repository root.

```sh
javac --release 25 -d .local/build/jobs/graphics-generator src/prepare/java/com/voyemu/prepare/Json.java src/app/graphics/generator/GenerateGraphics.java
java -cp .local/build/jobs/graphics-generator com.voyemu.prepare.GenerateGraphics --api src/app/graphics/generator/api.json --uniforms src/app/graphics/generator/uniform_names.json --catalog profiles/common/components/canbus/input/oem-mcu-fields.json --out src/app/graphics/generated_graphics.inc
```

`component.json` records the inputs, the output and their origins. The generated uniform schema derives from the SHA-256 of the uniform metadata it reads. Keep the existing wire catalog identity separate from the profile JSON serialization hash.

The API metadata contains Voyemu protocol semantics and standard function declarations, not downloaded headers or implementations. Uniform metadata contains observed identifier names only, not OEM shader code. The catalog remains in its existing profile location rather than being duplicated here.

Regeneration requires only the files named in the commands above. Unchanged inputs reproduce the checked-in output byte for byte; after regeneration, review the diff of `src/app/graphics/generated_graphics.inc`.
