from abc import ABCMeta, abstractmethod, abstractproperty
import os
import re
import json
import globalDictionaries
import configTemplates
from dataset import Dataset
from helperFunctions import replaceByMap, addIndex, getCommandOutput2
from TkAlExceptions import AllInOneError

class ValidationMetaClass(ABCMeta):
    sets = ["mandatories", "optionals", "needpackages"]
    dicts = ["defaults"]
    def __new__(cls, clsname, bases, dct):
        for setname in cls.sets:
            if setname not in dct: dct[setname] = set()
            dct[setname] = set.union(dct[setname], *(getattr(base, setname) for base in bases if hasattr(base, setname)))

        for dictname in cls.dicts:
            if dictname not in dct: dct[dictname] = {}
            for base in bases:
                if not hasattr(base, dictname): continue
                newdict = getattr(base, dictname)
                for key in set(newdict) & set(dct[dictname]):
                    if newdict[key] != dct[dictname][key]:
                        raise ValueError("Inconsistent values of defaults[{}]: {}, {}".format(key, newdict[key], dct[dictname][key]))
                dct[dictname].update(newdict)
        return super(ValidationMetaClass, cls).__new__(cls, clsname, bases, dct)

class GenericValidation(object):
    __metaclass__ = ValidationMetaClass
    defaultReferenceName = "DEFAULT"
    mandatories = set()
    defaults = {
                "cmssw":        os.environ['CMSSW_BASE'],
                "parallelJobs": "1",
                "jobid":        "",
               }
    needpackages = {"Alignment/OfflineValidation"}
    optionals = {"jobmode"}

    def __init__(self, valName, alignment, config, valType):
        import random
        self.name = valName
        self.valType = valType
        self.alignmentToValidate = alignment
        self.general = config.getGeneral()
        self.randomWorkdirPart = "%0i"%random.randint(1,10e9)
        self.configFiles = []
        self.config = config
        self.jobid = ""

        theUpdate = config.getResultingSection(valType+":"+self.name,
                                               defaultDict = self.defaults,
                                               demandPars = self.mandatories)
        self.general.update(theUpdate)
        self.jobmode = self.general["jobmode"]
        self.NJobs = int(self.general["parallelJobs"])

        # limit maximum number of parallel jobs to 40
        # (each output file is approximately 20MB)
        maximumNumberJobs = 40
        if self.NJobs > maximumNumberJobs:
            msg = ("Maximum allowed number of parallel jobs "
                   +str(maximumNumberJobs)+" exceeded!!!")
            raise AllInOneError(msg)
        if self.NJobs > 1 and not isinstance(self, ParallelValidation):
            raise AllInOneError("Parallel jobs not implemented for {}!\n"
                                "Please set parallelJobs = 1.".format(type(self).__name__))

        self.jobid = self.general["jobid"]
        if self.jobid:
            try:  #make sure it's actually a valid jobid
                output = getCommandOutput2("bjobs %(jobid)s 2>&1"%self.general)
                if "is not found" in output: raise RuntimeError
            except RuntimeError:
                raise AllInOneError("%s is not a valid jobid.\nMaybe it finished already?"%self.jobid)

        self.cmssw = self.general["cmssw"]
        badcharacters = r"\'"
        for character in badcharacters:
            if character in self.cmssw:
                raise AllInOneError("The bad characters " + badcharacters + " are not allowed in the cmssw\n"
                                    "path name.  If you really have it in such a ridiculously named location,\n"
                                    "try making a symbolic link somewhere with a decent name.")
        try:
            os.listdir(self.cmssw)
        except OSError:
            raise AllInOneError("Your cmssw release " + self.cmssw + ' does not exist')

        if self.cmssw == os.environ["CMSSW_BASE"]:
            self.scramarch = os.environ["SCRAM_ARCH"]
            self.cmsswreleasebase = os.environ["CMSSW_RELEASE_BASE"]
        else:
            command = ("cd '" + self.cmssw + "' && eval `scramv1 ru -sh 2> /dev/null`"
                       ' && echo "$CMSSW_BASE\n$SCRAM_ARCH\n$CMSSW_RELEASE_BASE"')
            commandoutput = getCommandOutput2(command).split('\n')
            self.cmssw = commandoutput[0]
            self.scramarch = commandoutput[1]
            self.cmsswreleasebase = commandoutput[2]

        self.packages = {}
        for package in self.needpackages:
            for placetolook in self.cmssw, self.cmsswreleasebase:
                pkgpath = os.path.join(placetolook, "src", package)
                if os.path.exists(pkgpath):
                    self.packages[package] = pkgpath
                    break
            else:
                raise AllInOneError("Package {} does not exist in {} or {}!".format(package, self.cmssw, self.cmsswreleasebase))

        self.AutoAlternates = True
        if config.has_option("alternateTemplates","AutoAlternates"):
            try:
                self.AutoAlternates = json.loads(config.get("alternateTemplates","AutoAlternates").lower())
            except ValueError:
                raise AllInOneError("AutoAlternates needs to be true or false, not %s" % config.get("alternateTemplates","AutoAlternates"))

        knownOpts = set(self.defaults.keys())|self.mandatories|self.optionals
        ignoreOpts = []
        config.checkInput(valType+":"+self.name,
                          knownSimpleOptions = knownOpts,
                          ignoreOptions = ignoreOpts)

    def getRepMap(self, alignment = None):
        from plottingOptions import PlottingOptions
        if alignment == None:
            alignment = self.alignmentToValidate
        try:
            result = PlottingOptions(self.config, self.valType)
        except KeyError:
            result = {}
        result.update(alignment.getRepMap())
        result.update(self.general)
        result.update({
                "workdir": os.path.join(self.general["workdir"],
                                        self.randomWorkdirPart),
                "datadir": self.general["datadir"],
                "logdir": self.general["logdir"],
                "CommandLineTemplate": ("#run configfile and post-proccess it\n"
                                        "cmsRun %(cfgFile)s\n"
                                        "%(postProcess)s "),
                "CMSSW_BASE": self.cmssw,
                "SCRAM_ARCH": self.scramarch,
                "CMSSW_RELEASE_BASE": self.cmsswreleasebase,
                "alignmentName": alignment.name,
                "condLoad": alignment.getConditions(),
                "LoadGlobalTagTemplate": configTemplates.loadGlobalTagTemplate,
                })
        result.update(self.packages)
        return result

    @abstractproperty
    def filesToCompare(self):
        pass

    def getCompareStrings( self, requestId = None, plain = False ):
        result = {}
        repMap = self.getRepMap().copy()
        for validationId in self.filesToCompare:
            repMap["file"] = self.filesToCompare[ validationId ]
            if repMap["file"].startswith( "/castor/" ):
                repMap["file"] = "rfio:%(file)s"%repMap
            elif repMap["file"].startswith( "/store/" ):
                repMap["file"] = "root://eoscms.cern.ch//eos/cms%(file)s"%repMap
            if plain:
                result[validationId]=repMap["file"]
            else:
                result[validationId]= "%(file)s=%(title)s|%(color)s|%(style)s"%repMap
        if requestId == None:
            return result
        else:
            if not "." in requestId:
                requestId += ".%s"%self.defaultReferenceName
            if not requestId.split(".")[-1] in result:
                msg = ("could not find %s in reference Objects!"
                       %requestId.split(".")[-1])
                raise AllInOneError(msg)
            return result[ requestId.split(".")[-1] ]

    def createFiles(self, fileContents, path, repMap = None, repMaps = None):
        """repMap: single map for all files
           repMaps: a dict, with the filenames as the keys"""
        if repMap is not None and repMaps is not None:
            raise AllInOneError("createFiles can only take repMap or repMaps (or neither), not both")
        result = []
        for fileName in fileContents:
            filePath = os.path.join(path, fileName)
            result.append(filePath)

            for (i, filePathi) in enumerate(addIndex(filePath, self.NJobs)):
                theFile = open( filePathi, "w" )
                fileContentsi = fileContents[ fileName ]
                if repMaps is not None:
                    repMap = repMaps[fileName]
                if repMap is not None:
                    repMap.update({"nIndex": str(i)})
                    fileContentsi = replaceByMap(fileContentsi, repMap)
                theFile.write( fileContentsi )
                theFile.close()

        return result

    def createConfiguration(self, fileContents, path, schedule = None, repMap = None, repMaps = None):
        self.configFiles = self.createFiles(fileContents,
                                            path, repMap = repMap, repMaps = repMaps)
        if not schedule == None:
            schedule = [os.path.join( path, cfgName) for cfgName in schedule]
            for cfgName in schedule:
                if not cfgName in self.configFiles:
                    msg = ("scheduled %s missing in generated configfiles: %s"
                           %(cfgName, self.configFiles))
                    raise AllInOneError(msg)
            for cfgName in self.configFiles:
                if not cfgName in schedule:
                    msg = ("generated configuration %s not scheduled: %s"
                           %(cfgName, schedule))
                    raise AllInOneError(msg)
            self.configFiles = schedule
        return self.configFiles

    def createScript(self, fileContents, path, downloadFiles=[], repMap = None, repMaps = None):
        self.scriptFiles = self.createFiles(fileContents,
                                            path, repMap = repMap, repMaps = repMaps)
        for script in self.scriptFiles:
            for scriptwithindex in addIndex(script, self.NJobs):
                os.chmod(scriptwithindex,0o755)
        return self.scriptFiles

    def createCrabCfg(self, fileContents, path ):
        if self.NJobs > 1:
            msg =  ("jobmode 'crab' not supported for parallel validation."
                    " Please set parallelJobs = 1.")
            raise AllInOneError(msg)
        self.crabConfigFiles = self.createFiles(fileContents, path)
        return self.crabConfigFiles


class GenericValidationData(GenericValidation):
    """
    Subclass of `GenericValidation` which is the base for validations using
    datasets.
    """
    needParentFiles = False
    mandatories = {"dataset", "maxevents"}
    defaults = {
                "runRange": "",
                "firstRun": "",
                "lastRun": "",
                "begin": "",
                "end": "",
                "JSON": ""
               }
    
    def __init__(self, valName, alignment, config, valType):
        """
        This method adds additional items to the `self.general` dictionary
        which are only needed for validations using datasets.
        
        Arguments:
        - `valName`: String which identifies individual validation instances
        - `alignment`: `Alignment` instance to validate
        - `config`: `BetterConfigParser` instance which includes the
                    configuration of the validations
        - `valType`: String which specifies the type of validation
        """

        super(GenericValidationData, self).__init__(valName, alignment, config, valType)

        # if maxevents is not specified, cannot calculate number of events for
        # each parallel job, and therefore running only a single job
        if int( self.general["maxevents"] ) == -1 and self.NJobs > 1:
            msg = ("Maximum number of events (maxevents) not specified: "
                   "cannot use parallel jobs.")
            raise AllInOneError(msg)

        tryPredefinedFirst = (not self.jobmode.split( ',' )[0] == "crab" and self.general["JSON"]    == ""
                              and self.general["firstRun"] == ""         and self.general["lastRun"] == ""
                              and self.general["begin"]    == ""         and self.general["end"]     == "")

        if self.general["dataset"] not in globalDictionaries.usedDatasets:
            globalDictionaries.usedDatasets[self.general["dataset"]] = {}

        if self.cmssw not in globalDictionaries.usedDatasets[self.general["dataset"]]:
            if globalDictionaries.usedDatasets[self.general["dataset"]] != {}:
                print ("Warning: you use the same dataset '%s' in multiple cmssw releases.\n"
                       "This is allowed, but make sure it's not a mistake") % self.general["dataset"]
            globalDictionaries.usedDatasets[self.general["dataset"]][self.cmssw] = {False: None, True: None}

        if globalDictionaries.usedDatasets[self.general["dataset"]][self.cmssw][tryPredefinedFirst] is None:
            dataset = Dataset(
                self.general["dataset"], tryPredefinedFirst = tryPredefinedFirst,
                cmssw = self.cmssw, cmsswrelease = self.cmsswreleasebase )
            globalDictionaries.usedDatasets[self.general["dataset"]][self.cmssw][tryPredefinedFirst] = dataset
            if tryPredefinedFirst and not dataset.predefined():                              #No point finding the data twice in that case
                globalDictionaries.usedDatasets[self.general["dataset"]][self.cmssw][False] = dataset

        self.dataset = globalDictionaries.usedDatasets[self.general["dataset"]][self.cmssw][tryPredefinedFirst]
        self.general["magneticField"] = self.dataset.magneticField()
        self.general["defaultMagneticField"] = "MagneticField"
        if self.general["magneticField"] == "unknown":
            print "Could not get the magnetic field for this dataset."
            print "Using the default: ", self.general["defaultMagneticField"]
            self.general["magneticField"] = '.oO[defaultMagneticField]Oo.'
        
        if not self.jobmode.split( ',' )[0] == "crab":
            try:
                self.general["datasetDefinition"] = self.dataset.datasetSnippet(
                    jsonPath = self.general["JSON"],
                    firstRun = self.general["firstRun"],
                    lastRun = self.general["lastRun"],
                    begin = self.general["begin"],
                    end = self.general["end"],
                    parent = self.needParentFiles )
            except AllInOneError as e:
                msg = "In section [%s:%s]: "%(valType, self.name)
                msg += str(e)
                raise AllInOneError(msg)
        else:
            if self.dataset.predefined():
                msg = ("For jobmode 'crab' you cannot use predefined datasets "
                       "(in your case: '%s')."%( self.dataset.name() ))
                raise AllInOneError( msg )
            try:
                theUpdate = config.getResultingSection(valType+":"+self.name,
                                                       demandPars = ["parallelJobs"])
            except AllInOneError as e:
                msg = str(e)[:-1]+" when using 'jobmode: crab'."
                raise AllInOneError(msg)
            self.general.update(theUpdate)
            if self.general["begin"] or self.general["end"]:
                ( self.general["begin"],
                  self.general["end"],
                  self.general["firstRun"],
                  self.general["lastRun"] ) = self.dataset.convertTimeToRun(
                    firstRun = self.general["firstRun"],
                    lastRun = self.general["lastRun"],
                    begin = self.general["begin"],
                    end = self.general["end"],
                    shortTuple = False)
                if self.general["begin"] == None:
                    self.general["begin"] = ""
                if self.general["end"] == None:
                    self.general["end"] = ""
                self.general["firstRun"] = str( self.general["firstRun"] )
                self.general["lastRun"] = str( self.general["lastRun"] )
            if ( not self.general["firstRun"] ) and \
                   ( self.general["end"] or self.general["lastRun"] ):
                self.general["firstRun"] = str(
                    self.dataset.runList()[0]["run_number"])
            if ( not self.general["lastRun"] ) and \
                   ( self.general["begin"] or self.general["firstRun"] ):
                self.general["lastRun"] = str(
                    self.dataset.runList()[-1]["run_number"])
            if self.general["firstRun"] and self.general["lastRun"]:
                if int(self.general["firstRun"]) > int(self.general["lastRun"]):
                    msg = ( "The lower time/runrange limit ('begin'/'firstRun') "
                            "chosen is greater than the upper time/runrange limit "
                            "('end'/'lastRun').")
                    raise AllInOneError( msg )
                self.general["runRange"] = (self.general["firstRun"]
                                            + '-' + self.general["lastRun"])
            try:
                self.general["datasetDefinition"] = self.dataset.datasetSnippet(
                    jsonPath = self.general["JSON"],
                    firstRun = self.general["firstRun"],
                    lastRun = self.general["lastRun"],
                    begin = self.general["begin"],
                    end = self.general["end"],
                    crab = True )
            except AllInOneError as e:
                msg = "In section [%s:%s]: "%(valType, self.name)
                msg += str( e )
                raise AllInOneError( msg )

    def getRepMap(self, alignment = None):
        result = super(GenericValidationData, self).getRepMap(alignment)
        outputfile = os.path.expandvars(replaceByMap(
                           "%s_%s_.oO[name]Oo..root" % (self.outputBaseName, self.name)
                                 , result))
        resultfile = os.path.expandvars(replaceByMap(("/store/caf/user/$USER/.oO[eosdir]Oo./" +
                           "%s_%s_.oO[name]Oo..root" % (self.resultBaseName, self.name))
                                 , result))
        result.update({
                "resultFile": ".oO[resultFiles[.oO[nIndex]Oo.]]Oo.",
                "resultFiles": addIndex(resultfile, self.NJobs),
                "finalResultFile": resultfile,
                "outputFile": ".oO[outputFiles[.oO[nIndex]Oo.]]Oo.",
                "outputFiles": addIndex(outputfile, self.NJobs),
                "finalOutputFile": outputfile
                })
        return result

    @property
    def cfgName(self):
        return "%s.%s.%s_cfg.py"%( self.configBaseName, self.name,
                                   self.alignmentToValidate.name )

    @abstractproperty
    def cfgTemplate(self):
        pass

    @property
    def filesToCompare(self):
        return {self.defaultReferenceName: self.getRepMap()["finalResultFile"]}

    def createConfiguration(self, path ):
        repMap = self.getRepMap()
        cfgs = {self.cfgName: self.cfgTemplate}
        super(GenericValidationData, self).createConfiguration(cfgs, path, repMap=repMap)

    def createScript(self, path, template = configTemplates.scriptTemplate, downloadFiles=[], repMap = None, repMaps = None):
        scriptName = "%s.%s.%s.sh"%(self.scriptBaseName, self.name,
                                    self.alignmentToValidate.name )
        if repMap is None and repMaps is None:
            repMap = self.getRepMap()
            repMap["CommandLine"]=""
            for cfg in self.configFiles:
                repMap["CommandLine"]+= repMap["CommandLineTemplate"]%{"cfgFile":addIndex(cfg, self.NJobs, ".oO[nIndex]Oo."),
                                                      "postProcess":""
                                                     }
        scripts = {scriptName: template}
        return super(GenericValidationData, self).createScript(scripts, path, downloadFiles = downloadFiles,
                                                               repMap = repMap, repMaps = repMaps)

    def createCrabCfg(self, path, crabCfgBaseName):
        """
        Method which creates a `crab.cfg` for a validation on datasets.
        
        Arguments:
        - `path`: Path at which the file will be stored.
        - `crabCfgBaseName`: String which depends on the actual type of
                             validation calling this method.
        """
        crabCfgName = "crab.%s.%s.%s.cfg"%( crabCfgBaseName, self.name,
                                            self.alignmentToValidate.name )
        repMap = self.getRepMap()
        repMap["script"] = "dummy_script.sh"
        # repMap["crabOutputDir"] = os.path.basename( path )
        repMap["crabWorkingDir"] = crabCfgName.split( '.cfg' )[0]
        self.crabWorkingDir = repMap["crabWorkingDir"]
        repMap["numberOfJobs"] = self.general["parallelJobs"]
        repMap["cfgFile"] = self.configFiles[0]
        repMap["queue"] = self.jobmode.split( ',' )[1].split( '-q' )[1]
        if self.dataset.dataType() == "mc":
            repMap["McOrData"] = "events = .oO[nEvents]Oo."
        elif self.dataset.dataType() == "data":
            repMap["McOrData"] = "lumis = -1"
            if self.jobmode.split( ',' )[0] == "crab":
                print ("For jobmode 'crab' the parameter 'maxevents' will be "
                       "ignored and all events will be processed.")
        else:
            raise AllInOneError("Unknown data type!  Can't run in crab mode")
        crabCfg = {crabCfgName: replaceByMap( configTemplates.crabCfgTemplate,
                                              repMap ) }
        return super(GenericValidationData, self).createCrabCfg( crabCfg, path )

class ParallelValidation(GenericValidation):
    @classmethod
    def initMerge(cls):
        return ""
    @abstractmethod
    def appendToMerge(self):
        pass

    @classmethod
    def doInitMerge(cls):
        result = self.initmerge()
        result = replaceByMap(result, PlottingOptions(None, cls))
        if result and result[-1] != "\n": result += "\n"
        return result
    def doMerge(self):
        result = self.appendToMerge()
        result += ("if [[ tmpMergeRetCode -eq 0 ]]; then\n"
                   "  xrdcp -f .oO[finalOutputFile]Oo. root://eoscms//eos/cms.oO[finalResultFile]Oo.\n"
                   "fi\n"
                   "if [[ ${tmpMergeRetCode} -gt ${mergeRetCode} ]]; then\n"
                   "  mergeRetCode=${tmpMergeRetCode}\n"
                   "fi\n")
        result = replaceByMap(result, self.getRepMap())
        if result[-1] != "\n": result += "\n"
        return result

class ValidationWithPlots(GenericValidation):
    @classmethod
    def runPlots(cls, validations):
        return ("rfcp .oO[plottingscriptpath]Oo. ."
                "root -x -b -q .oO[plottingscriptpath]Oo.")
    @abstractmethod
    def appendToPlots(self):
        pass
    @abstractmethod
    def plottingscriptname(cls):
        """override with a classmethod"""
    @abstractmethod
    def plottingscripttemplate(cls):
        """override with a classmethod"""

    @classmethod
    def doRunPlots(cls, validations):
        cls.createPlottingScript(validations)
        result = cls.runPlots(validations)
        result = replaceByMap(result, PlottingOptions(None, cls))
        if result and result[-1] != "\n": result += "\n"
        return result
    @classmethod
    def createPlottingScript(cls, validations):
        repmap = PlottingOptions(None, cls).validation.copy()
        filename = replaceByMap(".oO[plottingscriptpath]Oo.", repmap)
        repmap["PlottingInstantiation"] = "".join(
                                                  replaceByMap(v.appendToPlots(), v.getRepMap())
                                                       for v in validations
                                                 )
        plottingscript = replaceByMap(cls.plottingscripttemplate(), repmap)
        with open(filename) as f:
            f.write(plottingscript)
