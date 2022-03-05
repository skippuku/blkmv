# Warning! Known issue can cause data loss!
blkmv currently fucks up royaly if you try to use it on directories with spaces in them... I will fix this eventually. Luckily, it doesn't seem like anyone is using this besides me right now. If you're reading this I wouldn't actually recommend using this right now for this reason. In the future I will have better testing to make sure things like this can't happen!

# blkmv
blkmv is a command line utility for reorganizing and renaming files using a text editor. Currently blkmv only supports GNU/Linux systems, but it might accidentally work on other unix-like systems. blkmv is inspered by the bulk rename tool in ranger. I find ranger to be slow and bulkrename was the only thing I actually used from ranger, so I wrote my own version and then added some features.

# installation
Clone the repository and run `sudo make install`.

# usage
### basic
Pass a directory to blkmv. For example: `blkmv ~/Music/Album`. blkmv will open whatever program the environment variable EDITOR is set to. From inside the editor you can change the names of the files listed. To commit these changes, save and close the file. blkmv will then rename any file that changed. If you don't want to change anything, you can close the file without saving and nothing will happen.

### -R recursion
You can pass the `-R` option to get all of the files recursively in a directory.

### -m creating directories
Normally, if you edit a filename to be in a directory that doesn't exist, the rename will fail, but if you pass the `-m` option, blkmv will create any directories needed to move the file.

### -e deleting empty directories
As you are moving files around you might empty out a directory. If you want blkmv to detect and delete empty directories, pass the `-e` option.

### the power of -Rem
`blkmv -Rem <directory>` combined with a powerful text editor can be used to completely reorganise a file tree very quickly. You can arbitrarily name files to where you would like them to be without thinking about what directories you need to create or remove.

### deleting files
From within the editor if you prepend a filename with `#` it will delete that file. **Don't try to rename files to something that starts with a #. blkmv will delete the file.**

### -D directory mode
By passing `-D` to blkmv, you will get a list of directories instead of files. Works the same way as normal mode, just with directories.

### using a different editor
If you want to use a different editor from the default once, you have a couple options. You can set the EDITOR variable, or use the `--with` option.
For example, say you want to use nano. 
`EDITOR=nano blkmv <directory>`
`blkmv --with nano <directory>`

### other options
`-f` shows full file paths so you can move file outside of the directory you opened.
`-h` shows hidden files.
`-q` By default, blkmv reports what it's doing to stdout. This option hides that output.

### play around
I recommend creating a test folder with a bunch of garbage files and sub-directories to play around with blkmv and get a feel for how it works. Try using all of the options, especially `-R`, `-e`, and `-m`.
