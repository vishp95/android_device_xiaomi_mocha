isInitialized=false

# Patches
BIONIC_LIBM_PATCH=9c48399beef7f0b1a1cd14e0353b322027763484
FENCE_LEAKING_PATCH=7a3b1bae646b580fb67afa590f37eb9c5ed12403

function applyPatches() {
	root=$PWD
	cd $root/bionic
	git cherry-pick $BIONIC_LIBM_PATCH
	cd $root/hardware/interfaces
	git fetch "https://github.com/LineageOS/android_hardware_interfaces" refs/changes/97/265297/5 && git cherry-pick FETCH_HEAD
	cd $root
}

function initialization() {
	. build/envsetup.sh
	breakfast mocha
	isInitialized=true
}

function syncSources() {
	repo sync -j4 --force-sync
	echo
	echo
	echo "applying necessary patches"
	applyPatches
	main
}

function compilePackage() {
	echo
	echo
	echo "Package name:"
	read env
	mm $env
}

function buildTheROM() {
	echo
	echo
	echo "---------------------------------------------------"
	echo "Is clean build requested?                         -"
	echo "---------------------------------------------------"
	echo "1 - Yes                                           -"
	echo "---------------------------------------------------"
	echo "2 - No                                            -"
	echo "---------------------------------------------------"
	echo "3 - Go to main menu                               -"
	echo "---------------------------------------------------"
	printf %s "Your choice: "
	read env

	case $env in
		1) make clean;brunch mocha;;
		2) brunch mocha;;
		3) main;;
		*) buildTheROM;;
	esac
}

function main()
{
	echo
	echo
	echo "---------------------------------------------------"
	echo "Available actions:                                -"
	echo "---------------------------------------------------"
	echo "1 - Compile the ROM                               -"
	echo "---------------------------------------------------"
	echo "2 - Sync sources                                  -"
	echo "---------------------------------------------------"
	echo "3 - mm package                                    -"
	echo "---------------------------------------------------"
	echo "4 - Exit                                          -"
	echo "---------------------------------------------------"
	printf %s "Your choice: "
	read env

	case $env in
		1) buildTheROM;;
		2) syncSources;;
		3) compilePackage;;
		4) return;;
		*) main;;
	esac
}

if [[ $isInitialized == false ]]; then
	initialization
fi

main
