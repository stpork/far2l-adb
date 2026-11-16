// Run with the existing far2l-smoke harness and a TESTING=YES binary.
var mydir = WorkDir();
var shells = ["/bin/bash", "/bin/zsh"];
for (var i = 0; i < shells.length; ++i) {
    if (!Exists(shells[i])) { Log("SKIP missing shell " + shells[i]); continue; }
    var profile = mydir + "/profile-" + i;
    var left = mydir + "/left";
    var right = mydir + "/right";
    MkdirsAll([profile, left, right], 0700);
    var flags = i == 0 ? " --noprofile --norc -i" : " -f -i";
    StartApp(["--tty", "--nodetect", "--mortal", "-u", profile,
        "-cd", left, "-cd", right,
        "-set:Cmdline.UseShell=true", "-set:Cmdline.ShellCmd=" + shells[i] + flags,
        "-set:Cmdline.WaitKeypress=0"]);
    ExpectString("Help - FAR2L"); TypeEscape();
    ExpectString("OSC52"); TypeEscape();
    ExpectString("↑", -1, -2, 1, 1);
    for (var repeat = 0; repeat < 2; ++repeat) {
        TypeText("printf 'INTERRUPT_%s\\n' 'STARTED_" + repeat + "'; while sleep 1; do :; done");
        TypeEnter(); ExpectString("INTERRUPT_STARTED_" + repeat);
        ToggleLCtrl(true); TypeText("C"); ToggleLCtrl(false);
        ExpectString("↑", -1, -2, 1, 1);
        // Function keys must again belong to far2l, not the stranded shell.
        TypeFKey(9); ExpectString("Left    Files    Commands    Options    Right");
        TypeEscape(); ExpectString("↑", -1, -2, 1, 1);
    }
    TypeText("printf 'AFTER_%s\\n' 'INTERRUPT'");TypeEnter();
    ExpectString("AFTER_INTERRUPT"); ExpectString("↑", -1, -2, 1, 1);
    TypeText("exit far");TypeEnter();ExpectAppExit(0);
}
0;
