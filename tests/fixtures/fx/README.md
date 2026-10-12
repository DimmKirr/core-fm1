These binary fixtures were written by unmodified firmware at commit
`590e7cd2af42b7b35efb8e6aa01f72ba57155185`, using its real `proj_pack`,
`bank_pack` and `template_save` paths. They freeze the pre-FX FUN16, FBKK
and TPLF formats independently of the new decoder.

Track 1 has FM6 engine parameter E0 = 17 and an obsolete FX field set to 127.
Its first step is C/E/G with velocity 100. Another pattern bank has note 72.
The recording has onset 55, duration 217, owner 11 and pitch offset 3.
Motion addresses the old E0 ID, 96. The new decoder must move E0 to 100,
preserve this material and initialise all new FX slots dry.
