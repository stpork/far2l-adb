var root = WorkDir();
var profile = root + "/profile";
var left = root + "/left";
var right = root + "/right";
MkdirsAll([profile + "/.config/settings", left, right], 0700);
SaveTextFile(profile + "/.config/settings/config.ini", ["[Descriptions]", "UpdateMode=2"]);
SaveTextFile(left + "/a", ["new a"]);
SaveTextFile(left + "/b", ["new b"]);
SaveTextFile(left + "/Descript.ion", ["a first", " continuation", "b second"]);
SaveTextFile(right + "/a", ["original"]);
var descriptions = right + "/Descript.ion";
SaveTextFile(descriptions, ["old previous description"]);

StartApp(["--tty", "--nodetect", "--mortal", "-u", profile, "-cd", left, "-cd", right]);
ExpectString("RenMov");
TypeDown();
TypeIns();
TypeIns();

function startOverwrite() {
    TypeFKey(5);
    ExpectString("════ Copy ═════");
    TypeEnter();
    ExpectString("already exists");
}

startOverwrite();
TypeEscape();
ExpectNoString("already exists");
// Both source files must still be marked. Retrying F5 must copy both.
startOverwrite();
// Another operation commits after this copy has read the destination list.
SaveTextFile(descriptions, ["old previous description", "other concurrent description"]);
LClickWhereFound(ExpectString("Overwrite"));
ExpectNoString("already exists");
ExpectNoString("12 bytes in 2 files", 0, 0, 60, 80);
ExpectString("RenMov");
TypeFKey(10);
ExpectString("Do you want to quit FAR?");
TypeEnter();
ExpectAppExit(0);

if (!CheckFilesDataSame(left + "/a", right + "/a") ||
    !CheckFilesDataSame(left + "/b", right + "/b")) Panic("Retry lost source selection");
var entries = LoadTextFile(descriptions).join("\n");
if (entries.indexOf("other concurrent description") < 0 ||
    entries.indexOf("old previous description") < 0 ||
    entries.indexOf("a first\n continuation") < 0 ||
    entries.indexOf("b second") < 0) Panic("Missing descriptions after overwrite: " + entries);
0;
