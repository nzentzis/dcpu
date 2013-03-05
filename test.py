#!/usr/bin/python

import subprocess, re, os, os.path
import xml.etree.ElementTree as ET
import sys, tempfile, shutil

DAS_URLS = {"linux": "https://github.com/downloads/jonpovey/das/das-v0.16.tar.gz",
		"mac": "https://github.com/downloads/jonpovey/das/bluedas-v0.13-OSX-64bit.tar.gz",
		"windows": "https://github.com/downloads/jonpovey/das/das-v0.17.zip"}
DAS_NAME = {"linux": "das", "mac": "das", "windows": "das.exe"}

def flprint(s):
	sys.stdout.write(s)
	sys.stdout.flush()

def installDAS(url, fn):
	# Download to a temporary file
	import urllib.request, urllib.error
	tf = tempfile.TemporaryFile()

	flprint("Connecting...")
	try:
		req = urllib.request.urlopen(url)
	except urllib.error.HTTPError as e:
		print("Failed - HTTP %d - %s" % (e.code, e.reason))
		raise RuntimeError()
	except urllib.error.URLError:
		print("Failed")
		raise RuntimeError()
	clen = int(req.getheader("Content-Length", default=1))
	recvBytes = 0
	fmt = "\rDownloaded %0"+str(len(str(clen)))+("d / %d" % clen)
	while True:
		d = req.read(clen-recvBytes)
		tf.write(d)
		recvBytes += len(d)
		if(len(d) == 0):
			break
		flprint(fmt % recvBytes)
	print("\nDownload complete")
	tf.seek(0)
	
	# Decompress it
	if(url.endswith("zip")):
		import zipfile
		zf = zipfile.ZipFile(tf)
		zf.extractall(".dcpu_test_cache")
		# TODO: Make this work
	elif(url.endswith("tar.gz")):
		import tarfile
		tar = tarfile.open(fileobj=tf, mode="r|gz")
		
		# Find the path of the das executable
		items = tar.getnames()
		tar.close()
		dasMember = list(filter(lambda x: x.endswith("das"), items))
		if(len(dasMember) == 0):
			print("Cannot find DAS binary in downloaded file")
			raise RuntimeError()
		dasMember = dasMember[0]
		
		# Extract to a temporary directory
		tf.seek(0)
		tar = tarfile.open(fileobj=tf, mode="r|gz")
		tdir = tempfile.TemporaryDirectory()
		with tdir:
			tdn = tdir.name
			tar.extractall(tdn)
			
			# Move das to the correct place
			shutil.move(os.path.join(tdn, dasMember), fn)

# Make sure DAS is installed in .dcpu_test_cache. Returns
# path to DAS executable
def checkForDAS():
	flprint("Checking for DAS...")
	if(not os.path.exists(".dcpu_test_cache")):
		os.mkdir(".dcpu_test_cache")
	pf = sys.platform
	if(pf.startswith("darwin")):
		pf = "mac"
	elif(pf.startswith("win") or pf.startswith("cygwin")):
		pf = "windows"
	elif(pf.startswith("linux")):
		pf = "linux"
	else:
		raise RuntimeError("Cannot determine local platform")
	dasfile = DAS_URLS[pf]
	
	# Check for das
	dasExec = os.path.join(".dcpu_test_cache", DAS_NAME[pf])
	if(not os.path.exists(dasExec)):
		print("Not Found - Downloading")
		installDAS(dasfile, dasExec)
	else:
		print("Found")
	return dasExec

# Check for DAS
dasExecutable = checkForDAS()
assemblyDir = tempfile.mkdtemp()

# Returns the assembled path or None on failure
def assembleFile(name):
	# Build a temporary file
	fqn = os.getcwd()
	outName = os.path.join(assemblyDir, name+".bin")
	fqName = os.path.join(fqn, "tests", name)
	rc = subprocess.call([dasExecutable, "-o", outName, fqName])
	if(rc != 0):
		return None
	return outName

# Search for test cases
testFiles = os.listdir("tests")
testFiles = filter(lambda x: x.endswith(".xml"), testFiles)

# Iterate through test cases and parse them
failed = []
passed = []
for ctest in testFiles:
	print("Testing '%s'" % ctest)
	try:
		fdata = open(os.path.join("tests", ctest), "r").read()
	except IOError:
		print("Cannot open test file '%s'" % ctest)
		failed.append((ctest,None))
		continue
	root = ET.fromstring(fdata)
	
	# Parse the tree
	source = root.find("source")
	cycles = root.find("cycles")
	name = root.find("name")
	results = root.find("results")
	if(source == None):
		print("\tFailed - Invalid Test: No source specified")
		failed.append((ctest,None))
		continue
	if(name == None):
		print("\tFailed - Invalid Test: No name specified")
		failed.append((ctest,None))
		continue
	if(results == None):
		print("\tFailed - Invalid Test: No result constraints specified")
		failed.append((ctest,None))
		continue
	if(cycles == None):
		print("\tFailed - Invalid Test: No cycle count specified")
		failed.append((ctest,None))
		continue
	source = source.text
	name = name.text
	cycles = cycles.text
	resultRegs = results.findall("register")
	resultConstraints = []
	for i in resultRegs:
		attrs = i.attrib
		if('name' not in attrs or 'value' not in attrs):
			print("\tWarning: Invalid result constraint")
			continue
		resultConstraints.append((attrs['name'], eval(attrs["value"])))
	
	# Try assembling the source file
	binPth = assembleFile(source)
	if(binPth == None):
		print("\tFailed - Assembly of input source failed")
		failed.append((ctest,name))
		continue
	
	# Run the test
	try:
		emu = subprocess.check_output(["./dcpu", "--test", "--cycles", cycles, binPth])
	except subprocess.CalledProcessError:
		print("\tFailed - Emulator returned invalid retcode")
		failed.append((ctest,name))
		continue
	except OSError:
		print("\tFailed - Cannot find emulator binary")
		failed.append((ctest,name))
		continue
	
	# Parse the output
	emu = emu.decode("utf-8")
	lines = emu.splitlines()
	linePattern = re.compile("([A-Z]+)\s*=([0-9a-f]{4})")
	regs = {}
	for i in lines:
		m = linePattern.match(i)
		if(m == None):
			print("\tWARNING - Emulator returned incomprehensible line: '%s'" % i)
			continue
		regs[m.group(1).lower()] = int("0x"+m.group(2), 16)
	
	# Check against the XML
	failedRegs = []
	for i in resultConstraints:
		if(regs[i[0]] != i[1]):
			failedRegs.append(i[0])
	if(len(failedRegs) > 0):
		print("\tFailed - Register values invalid")
		for i in failedRegs:
			print("\t\tName - Correct - Actual")
			rn = i.toupper()
			correct = resultConstraints[i]
			print("\t\t%4s - 0x%04x  - 0x%04x" % (i,correct,regs[i]))
		failed.append((ctest,name))
		continue
	
	# Done!
	print("\tPassed: %s" % name)
	passed.append((ctest,name))
	
# Clean up our temporary directory
shutil.rmtree(assemblyDir)

# Print a summary
print("\n\n")
print("-"*80)
print("RESULT SUMMARY".center(80,"-"))
print("-"*80)

print("Passed: %d" % len(passed))
for i in passed:
	print("\t%s\t- %s" % i)
print("Failed: %d" % len(failed))
for i in failed:
	if(i[1] == None):
		print("\t%s\t- Unknown" % i[0])
	else:
		print("\t%s\t- %s" % i)
print("Passed %%: %f" % ((len(passed) / (len(passed)+len(failed)))*100))
print("Failed %%: %f" % ((len(failed) / (len(passed)+len(failed)))*100))

# And exit
if(len(failed) > 0):
	sys.exit(1)
else:
	sys.exit(0)
