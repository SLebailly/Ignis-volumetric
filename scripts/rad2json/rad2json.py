#!/bin/python3
# Converts radiance scene file to Ignis scene description

#TODO:
#handle -i flag in xform when passed on its own (i.e. independent of -a flag)
#process the .rad files passed with xform
#make sure nameCount variable functions as expected
#simplify how the transformation matrix of the camera (and others) is passed: a lookat dictionary can be passed now. (Note target is a position, target = origin + direction).
#get rid of config.py

import argparse
import numpy as np
from pathlib import Path
import os
import logging
import config
from parseRad import parseRad
from restructurePrimitives import restructurePrimitives



if __name__ == "__main__":
    
    parser = argparse.ArgumentParser()
    parser.add_argument('InputFile', type=Path,
                        help='Path of the .rad file to be parsed.')
    parser.add_argument('OutputFile', nargs='?', type=Path, default=None,
                        help='Path to export')
    parser.add_argument('-d', '--debug', help="Print lots of debugging statements",
                        action="store_const", dest="loglevel", const=logging.DEBUG,
                        default=logging.WARNING,)
    parser.add_argument('-v', '--verbose', help="Be verbose",
                        action="store_const", dest="loglevel", const=logging.INFO,)
    args = parser.parse_args()    
    logging.basicConfig(level=args.loglevel)
    # logging.basicConfig(level=args.loglevel, filename='mts2json.log', format='%(asctime)s %(levelname)s:%(message)s')


    #parse the input file
    radFilePath = args.InputFile
    if (not radFilePath.suffix == '.rad'):
        logging.error("The specified input file is not a .rad file.")
        exit()
    if (not radFilePath.exists()):
        logging.error("The specified input file does not exist.")
        exit()
    
    radFilePath = os.path.join(".",radFilePath)         #explicitly giving it a relative path. else the next line wont work
    config.RAD_FOLDER_PATH = os.path.relpath(os.path.dirname(radFilePath))
    logging.info(f".rad file relative folder: {config.RAD_FOLDER_PATH}")

    with open(radFilePath, 'r') as radFile:
        declarations = parseRad(radFile)

    #structure the data
    jsonData = restructurePrimitives(declarations)

    #write data in the output file
    jsonFilePath = args.OutputFile
    if jsonFilePath is None:
        jsonFilePath = os.path.splitext(radFilePath)[0]+'.json'
    with open(jsonFilePath, "w") as jsonFile:
        jsonFile.write(jsonData)

    logging.info(f"Scene written to {jsonFilePath}.")
