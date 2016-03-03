#!/usr/bin/env python

# Some idiot interpreters don't always set up __file__
# (I'm looking at you PythonWin)
try:
    __file__
except NameError:
    import sys
    __file__ = sys.argv[0]
    del sys

import SimpleXMLRPCServer
import os
import subprocess as subprocess_
import socket
import time
import sys
import getopt
import ConfigParser

subprocess = subprocess_
linesep = '\n'
SCRIPTDIR = os.path.dirname(__file__)

def write_(*args):
  """Place-holder for print"""
  if args:
    print ' '.join(map(str, args))
  else:
    print
write = write_

class PopenWrapper(object):
  """Like popen but actually writes stuff out to a log file too."""
  class WaitWrapper(object):
    """Wraps the wait object."""
    def __init__(self, waiter, log, cmdline):
        super(PopenWrapper.WaitWrapper, self).__init__()
        self.waiter = waiter
        self.log = log
        self.cmdline = cmdline
    def __getattr__(self, name):
        return getattr(self.waiter, name)
    def wait(self, *args, **kwargs):
        cmdline = self.cmdline
        self.log.write(linesep + 'EXECUTING: ' + cmdline)
        start = time.time()
        result = self.waiter.wait(*args, **kwargs)
        end = time.time()
        self.log.write(('FINISHED EXECUTING (%.02f' % (end - start)) + '): ' + cmdline + linesep)
        return result

  def __init__(self, logfile):
    """Initialise this PopenWrapper instance.
        Parameters:
          logfile  - File to write any output to.
    """
    super(PopenWrapper, self).__init__()
    self.logfile = logfile
    
  def Popen(self, *unargs, **kwargs):
    """Wrapper around the real popen. Dump the command line...
        Parameters:
          cmdline  - Command line being executed.
    """
    cmdline = None
    if 'args' in kwargs:
        cmdline = kwargs['args']
    else:
      cmdline = unargs[0]
    return PopenWrapper.WaitWrapper(subprocess_.Popen(*unargs, **kwargs), self, cmdline)

  def write(self, *args):
    """Placeholder for print"""
    write_(*args) #Print in build server.
    if args:
        self.logfile.write(' '.join(map(str, args)) + linesep) #Print to log file.
        self.logfile.flush()
    else:
        self.logfile.write(linesep)
        self.logfile.flush()

class FakeLog(object):
    """Used when log hasn't been created."""
    def close(self):
        """Place-holder close command."""
        pass

class RPCInterface(object):
    def make(self,build='',branch='master',certname='',developer=False):
        """make(build,branch,certname,developer) - Call powershell scripts to do the real work - Ex: make("123456","master","developer",False)"""
        log = FakeLog()
        start = time.time()
              
        try:
            major = int(major)
        except ValueError:
            raise Exception, "major must be an integer between 1 and 65535"
        if (major < 1) or (major > 65536):
            raise Exception, "major must be an integer between 1 and 65535"
        try:
            minor = int(minor)
        except ValueError:
            raise Exception, "minor must be an integer between 0 and 65535"
        if (minor < 0) or (minor > 65536):
            raise Exception, "minor must be an integer between 0 and 65535"
        try:
            micro = int(micro)
        except ValueError:
            raise Exception, "micro must be an integer between 0 and 65535"
        if (micro < 0) or (micro > 65536):
            raise Exception, "micro must be an integer between 0 and 65535"
        try:
            build = int(build)
        except ValueError:
            raise Exception, "build must be an integer between 1 and 65535"
        if (build < 1) or (build > 65536):
            raise Exception, "build must be an integer between 1 and 65535"

        if config != 'Debug' and config != 'Release':		
            raise Exception, "config must be either Debug or Release"

        uid = str(uid)
        #Initialize UID to timestamp if null
        if (uid == ''):
            uid = time.strftime("%Y%d%m-%H%M%S")

        #Work out whql path.
        whqldir = os.path.join(BUILDDIR, 'whql')
        whqlpath = ''
        if whql != '':
            whqlpath = os.path.join(whqldir, whql +'.zip')
            if not os.path.exists(whqlpath):
                raise Exception(whqlpath + " does not exist")

        #Work out ozbuild.
        if (ozbuild == ''):
            ozbuild = '0'
        try:
            ozbuild = int(ozbuild)
        except ValueError:
            raise Exception, "ozbuild must be an integer"
        
        #Create log directory/file
        try:
            os.chdir(BUILDDIR)
            if not os.path.exists(BUILDDIR + uid):
                os.makedirs(uid)
            if not os.path.exists(LOGDIR + uid):
                os.chdir(LOGDIR)
                os.makedirs(uid)
            os.chdir(LOGDIR + uid)
            log = open('output.log',"w")
            print "Log file created @ \\\\" + socket.gethostname() + "\\" + uid + "\\output.log"
            subprocess = PopenWrapper(log)
            write = subprocess.write
            os.chdir(BUILDDIR + uid)
        except:
            raise Exception, "ERROR: Unable to create UID directory/log"

        write('make('+repr(tag)+',major='+repr(major)+',minor='+repr(minor)+\
              ',micro='+repr(micro)+',build='+repr(build)+',config='+\
              repr(config)+',uid='+repr(uid)+',signcert='+repr(signcert)+\
              ',whql='+repr(whql)+',ozbuild='+repr(ozbuild)+')')

        #Find latest ozbuild   
        if ozbuild == 0:    
            os.chdir('win-changelog')
            dirlist1 = os.listdir('.')
            dirlist2 = []
            for d in dirlist1:
                try:
                    dirlist2.append(int(d))
                except:
                    pass
            dirlist2.sort(reverse=True)
            ozbuild = dirlist2[0]
            write("Using Oz build: " + str(ozbuild))
            os.chdir('..')

        #Build PV drivers/xensetup.exe
        os.chdir('xc-windows')
        command = 'dostampinf.bat '+ DDKDIR +' '+ BUILDDIR + uid +'\\xc-windows '+ str(major)+'.'+str(minor)+'.'+str(micro)+'.'+str(build)
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        write("Building xc-windows x86...")
        command = 'dothebuild.bat '+ DDKDIR +' '+ BUILDDIR + uid +'\\xc-windows x86'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        write("Building xc-windows x64...")
        command = 'dothebuild.bat '+ DDKDIR +' '+ BUILDDIR + uid +'\\xc-windows x64'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        #HACK: Copy in the Windows 8 WDDM Vesa binaries if they are on the build machine (really nice stuffs this is).
        write("Copying Windows8 XenVesa files...")
        w8vesadir = os.path.join(BUILDDIR, 'bins\\win8vesa')
        command = 'copy ' + w8vesadir + '\\i386\\*.* .\\build\\i386 /y' 
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        command = 'copy ' + w8vesadir + '\\amd64\\*.* .\\build\\amd64 /y' 
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        #Sign drivers
        if config == 'Release' and signcert == True:
            write("Cross-signing drivers...")
            command = 'docrosssign.bat '+ BUILDDIR + uid +'\\xc-windows'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            command = 'doverifysign.bat'
            retCode = subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            if retCode != 0:
                log.close()
                raise Exception, "ERROR: Cross signing drivers failed. See \\\\" + socket.gethostname() + "\\" + uid + "\\output.log for details."
            if whqlpath != '':
                write("Unpacking " + whql + " driver package...")
                command = 'unzip -o '+ whqlpath +' -d '+ BUILDDIR + uid +'\\xc-windows\\'
                subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        else:
            command = 'dotestsign.bat '+ BUILDDIR + uid +'\\xc-windows'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        #Copy the EULA file into the PV drivers install directory
        write("Copying EULA file...")
        if os.path.exists('..\\xenclient-oe-extra\\recipes\\xenclient\\xenclient-eula\\EULA-en-us'):
            command = 'copy ..\\xenclient-oe-extra\\recipes\\xenclient\\xenclient-eula\\EULA-en-us .\\install\\License.txt /y' 
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        #Build xensetup.exe
        write("Building NSIS installer...")
        os.chdir('install')
        if not os.path.exists('License.txt'):
            subprocess.Popen('echo "License" > License.txt', shell = True)
        command = 'sed -e "s/!define CurrentMajorVersion.*$/!define CurrentMajorVersion '+str(major)+'/g" xensetup.nsi > temp.nsi && move /Y temp.nsi xensetup.nsi'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        command = 'sed -e "s/!define CurrentMinorVersion.*$/!define CurrentMinorVersion '+str(minor)+'/g" xensetup.nsi > temp.nsi && move /Y temp.nsi xensetup.nsi'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        command = 'sed -e "s/!define CurrentMicroVersion.*$/!define CurrentMicroVersion '+str(micro)+'/g" xensetup.nsi > temp.nsi && move /Y temp.nsi xensetup.nsi'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        command = 'sed -e "s/!define CurrentBuildVersion.*$/!define CurrentBuildVersion '+str(build)+'/g" xensetup.nsi > temp.nsi && move /Y temp.nsi xensetup.nsi'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        command = 'makensis /DINSTALL_XENVESA /DINSTALL_XENVESA8 xensetup.nsi'
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
        os.chdir(BUILDDIR + uid)
 
        #Build win-tools
        write("Building win-tools...")
        command = 'powershell win-tools\\do_build.ps1 -c ' + config 
        subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()

        #Build XenClientTools.msi
        write("Building msi-installer...")
        command = 'powershell msi-installer\\XenClientTools\\do_build.ps1 -b ' + str(major)+'.'+str(minor)+'.'+str(micro)+'.'+str(build) + ' -c ' + config + ' -oz ' + str(ozbuild) +' -ozpath '+ OZPUBLIC + ' -tag '+ tag
        subprocess.Popen(command, shell = True, stdout = log, universal_newlines=True).wait()
	    
        #Check if target was successfully built and transfered
        if os.path.exists('msi-installer\\iso\\Packages\\XenClientTools.msi') and os.path.exists('msi-installer\\iso\\Packages\\XenClientTools64.msi'):
            write('Build completed successfully')
            #Create .iso
            #command = 'mkisofs -udf -V "xc-tools" -o msi-installer\\iso\\XenClientTools.iso msi-installer\\iso'
            command= 'ImgBurn.exe /mode build /outputmode imagefile /src msi-installer\\iso  /dest msi-installer\\iso\\XenClientTools.iso /filesystem "ISO9660 + UDF" /udfrevision "1.02" /verify yes /overwrite yes /VolumeLabel "xc-tools" /rootfolder yes /noimagedetails /start /close /includehiddenfiles yes'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            write('.iso Completed')
            #Move .iso to share
            if not os.path.exists(BUILDDIR + 'iso\\' + uid):
                os.chdir(BUILDDIR + 'iso')
                os.makedirs(uid)
            os.chdir(BUILDDIR)
            command = 'move /Y '+ uid +'\\msi-installer\\iso\\XenClientTools.iso iso\\'+ uid +'\\' 
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            write('.iso moved to share')
            #Create .zip of iso directory
            os.chdir(BUILDDIR+ uid +'\\msi-installer\\iso')
            command = 'zip -Sr '+BUILDDIR+'\iso\\'+ uid +'\\xc-tools.zip . -x *.iso -x *.mds'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            write('.zip created in share dir')
            #Create .zip of win-tools.git
            os.chdir(BUILDDIR+ uid +'\\win-tools')
            command = 'zip -r '+BUILDDIR+'\iso\\'+ uid +'\\win-tools.zip .'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            os.chdir(BUILDDIR)
            write('win-tools.zip moved to share')
            #Create .zip of xc-windows
            os.chdir(BUILDDIR+ uid+'\\xc-windows')
            command = 'zip -r '+BUILDDIR+'\iso\\'+ uid +'\\xc-windows.zip .'
            subprocess.Popen(command, shell = True, stdout = log, stderr = log, universal_newlines=True).wait()
            os.chdir(BUILDDIR)
            write('xc-windows.zip moved to share')
            #Calculate elapsed time
            end = time.time()
            elapsed = end - start
            write('Build finished in',elapsed,'seconds')
        else:
            #Calculate elapsed time
            end = time.time()
            elapsed = end - start
            write('Build failed after',elapsed,'seconds')
            log.close()
            raise Exception, "ERROR: XenClientTools failed to build. See \\\\" + socket.gethostname() + "\\" + uid + "\\output.log for details."

        log.close()
        return uid

    def clean(self, uid):
        """clean(uid) - Blows away projects."""
        #Create log directory/file
        # SNIP it did some munging of logs, see original
        #cleanpath = os.path.join(SCRIPTDIR, 'clean.ps1')
        #subprocess.Popen('powershell '+ cleanpath +' -uid '+ uid, stdout = log, stderr = log, universal_newlines=True).wait()
        write('Finished cleaning ' + uid)
        return 0

    def status(self):
        pass
        
    def retrieve_file(self, filename):
        pass
	
def main(argv):
	
	config = ""
	site = ""
	try:
		opts, args = getopt.getopt(argv, "hc:s:", ["help", "config=", "site="])
		for opt, arg in opts:
			if opt in ("-h", "--help"):
				usage()
				sys.exit()
			elif opt in ("-c", "--config"): 
				config = arg
			elif opt in ("-s", "--site"):
				site = arg
	except getopt.GetoptError:
		usage()
		sys.exit(2)
		
	loadConfig(config,site)
	s = SimpleXMLRPCServer.SimpleXMLRPCServer(('', PORT))
	s.register_introspection_functions()
	s.register_instance(RPCInterface())

	try:
		print """
		 XenClient Windows Build XMLRPC Server
	 
		 Use Control-C to exit
		 """
		s.serve_forever()
	except KeyboardInterrupt:
		print 'Exiting'
	
def loadConfig(cfg,site):
	try:
		config = ConfigParser.ConfigParser()
		config.read(cfg)
	except:
		print "Configuration file cannot be read."
		sys.exit()
	
	if not (config.has_section(site)):
		print "Invalid site specified. Available sites are:"
		print config.sections()
		sys.exit()
	else:
		try:
			global PORT, BUILDDIR, LOGDIR, CONFIG, GITURL, DDKDIR, OZPUBLIC
			PORT = config.getint(site,'port')
			BUILDDIR = config.get(site,'builddir')
			LOGDIR = config.get(site,'logdir')
			CONFIG = config.get(site,'config')
			GITURL = config.get(site,'giturl')
			DDKDIR = config.get(site,'ddkdir')
			OZPUBLIC = config.get(site,'ozpublic')
		except:
			print "Exception getting configuration option. Corrupt .cfg file? Missing option?"
			sys.exit()				

def usage():
	print """
	 Usage:
	 > python builddaemon.py -c "config file" -s "site"
	"""

if __name__ == '__main__':
	main(sys.argv[1:])
	
