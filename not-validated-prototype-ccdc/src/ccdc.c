#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>

#include "const.h"
#include "2d_array.h"
#include "utilities.h"
#include "input.h"
#include "output.h"
#include "ccdc.h"

#define NUM_LASSO_BANDS 5
#define TOTAL_BANDS 8
#define MAX_SCENES 63
int lasso_band_list[NUM_LASSO_BANDS] = {2, 3, 4, 5, 7};

/******************************************************************************
METHOD:  ccdc

PURPOSE:  the main routine for CCDC (Continuous Change Detection and 
          Classification) in C

RETURN VALUE:
Type = int
Value           Description
-----           -----------
ERROR           An error occurred during processing of the ccdc
SUCCESS         Processing was successful

PROJECT:  Land Change Monitoring, Assessment and Projection (LCMAP) Project

HISTORY:
Date        Programmer       Reason
--------    ---------------  -------------------------------------
1/15/2013   Song Guo         Original Development

NOTES: type ./ccdc --help for information to run the code
******************************************************************************/
int
main (int argc, char *argv[])
{
    char FUNC_NAME[] = "main";
    char msg_str[MAX_STR_LEN];  /* input data scene name */
    char filename[MAX_STR_LEN];         /* input binary filenames */
    char directory[MAX_STR_LEN];        /* input/output data directory */
    char extension[MAX_STR_LEN];        /* input TOA file extension */
    char data_dir[MAX_STR_LEN];         /* input/output data directory */
    char appendix[MAX_STR_LEN];         /* input TOA file extension */
    Input_t *input = NULL;      /* input data and meta data */
    char scene_name[MAX_STR_LEN];       /* input data scene name */
    char command[MAX_STR_LEN];
    float min_rmse;
    float t_cg;
    float t_max_cg;
    int conse;
    int status;                 /* return value from function call */
    //    Output_t *output = NULL; /* output structure and metadata */
    bool verbose;               /* verbose flag for printing messages */
    float alb = 0.1;
    int i, k, m;
    int num_points;
    char **scene_list = NULL;
    float **results = NULL;
    FILE *fd;
    int num_scenes;
    int i;
    int min_num_c = 4;
    int mid_num_c = 6;
    int max_num_c = 8;
    int num_c;                /* max number of coefficients for the model */
    int n_times = 3;          /* number of clear observations/coefficients*/
    int num_fc = 0;           /* intialize NUM of Functional Curves */
    float num_yrs = 365.25;   /* number of days per year */
    int num_byte = 2;         /* number of bytes: int16 */
    int nbands = 8;           /* bands 1-7, cfmask */
    int num_b1 = 2;           /* Band for multitemporal cloud/snow detection 
                                 (green) */ 
    int num_b2 = 5;           /* Band for multitemporal shadow/snow shadow 
                                 detection (SWIR) */
    int t_const = 400;        /* Threshold for cloud, shadow, and snow detection */
    int mini_yrs = 1;         /* minimum year for model intialization */
    int num_detect = NUM_LASSO_BANDS;/* number of bands for change detection */
    float p_min = 0.1;        /* percent of ref for mini_rmse */
    float t_ws = 0.95;        /* no change detection for permanent water pixels */
    float t_sn = 0.6;         /* no change detection for permanent snow pixels */ 
    float t_cs = 0.6;         /* Fmask fails threshold */
    float *sdate;
    Input_meta_t *meta;
    int row, col;
    int landsat_number;
    int loop_number;
    FILE *fp_bin[MAX_SCENES][TOTAL_BANDS];
    int fmask_sum = 0;

    time_t now;
    time (&now);
    snprintf (msg_str, sizeof(msg_str),
              "CCDC start_time=%s\n", ctime (&now));
    LOG_MESSAGE (msg_str, FUNC_NAME);

    /* Read the command-line arguments, including the name of the input
       Landsat TOA reflectance product and the DEM */
    status = get_args (argc, argv, &row, &col, &min_rmse, &t_cg, &t_max_cg, 
                       &conse, &verbose);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("calling get_args", FUNC_NAME, EXIT_FAILURE);
    }

    /* allocate memory for scene_list */
    scene_list = (char **) allocate_2d_array (MAX_SCENE_LIST, MAX_STR_LEN,
                                         sizeof (char));
    if (scene_list == NULL)
    {
        RETURN_ERROR ("Allocating scene_list memory", FUNC_NAME, FAILURE);
    }

    /* check if scene_list.txt file exists, if not, create the scene_list
       from existing files in the current data working directory */
    if (access("scene_list.txt", F_OK) != -1) /* File exists */
    {
        num_scenes = MAX_SCENE_LIST;
    }
    else /* File not exists */
    {
        stats = create_scene_list("L*_sr_band1.hdr", num_scenes, scene_list); 
    }

    fd = fopen("scene_list.txt", "r");
    if (fd == NULL)
    {
        RETURN_ERROR("Opening scene_list file", FUNC_NAME, FAILURE);
    }

    for (i = 0; i < num_scenes; i++)
    {
        if (fscanf(fd, "%s", scene_list[i]) == EOF)
        {
            RETURN_ERROR("Reading scene_list file", FUNC_NAME, FAILURE);
            num_scenes = i;
            break;
        }
    }

    /* Allocate memory for yeardoy */
    sdate = malloc(num_scenes * sizeof(float));
    if (sdate == NULL)
        RETURN_ERROR("ERROR allocating memory", FUNC_NAME, FAILURE);

    /* sort scene_list based on year & julian_day */
    status = sort_scene_based_on_year_doy(scene_list, num_scenes, sdate);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Calling sort_scene_based_on_year_jday", 
                      FUNC_NAME, EXIT_FAILURE);
    }

    /* Create the Input metadata structure */
    meta = (Input_meta_t *)malloc(sizeof(Input_meta_t));
    if (meta == NULL) 
        RETURN_ERROR("allocating Input data structure", FUNC_NAME, FAILURE);

    /* Get the metadata, all scene metadata are the same for stacked scenes */
    status = read_envi_header(scene_list[0], meta);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Calling sort_scene_based_on_year_jday", 
                      FUNC_NAME, EXIT_FAILURE);
    }

    if (verbose)
    {
        /* Print some info to show how the input metadata works */
        printf ("DEBUG: Number of input lines: %d\n", meta->lines);
        printf ("DEBUG: Number of input samples: %d\n", meta->samples);
        printf ("DEBUG: UL_MAP_CORNER: %f, %f\n", meta->upper_left_x,
                meta->upper_left_y);
        printf ("DEBUG: ENVI data type: %d\n", meta->data_type);
        printf ("DEBUG: ENVI byte order: %d\n", meta->byte_order);
        printf ("DEBUG: UTM zone number: %d\n", meta->utm_zone);
        printf ("DEBUG: Pixel size: %d\n", meta->pixel_size);
        printf ("DEBUG: Envi save format: %s\n", meta->interleave);
    }

    /* Open input files */
    short int buf[num_scenes][TOTAL_BANDS-1];
    unsigned char fmask_buf[num_scenes];
    /* Open input files */
    for (m = 0; m < loop_number; m++)
    {
        for (i = 0; i < MAX_SCENES; i++)
        {
            for (k = 0; k < TOTAL_BANDS; k++)
            {
                landsat_number = atoi(sub_string(scene_list[m*MAX_SCENES+i],2,1));
                if (landsat_number != 8)
                {
                    if (k == 5)
                        sprintf(filename, "%s_toa_band6.img", 
                                scene_list[m*MAX_SCENES+i]);
                    else if (k == 7)
                        sprintf(filename, "%s_cfmask.img", 
                                scene_list[m*MAX_SCENES+i]);
                    else
                        sprintf(filename, "%s_sr_band%d.img", 
                                scene_list[m*MAX_SCENES+i], k+1);
                }
                else
                {
                    if (k == 5)
                        sprintf(filename, "%s_toa_band10.img", 
                                scene_list[m*MAX_SCENES+i]);
                    else if (k == 7)
                        sprintf(filename, "%s_cfmask.img", 
                                scene_list[m*MAX_SCENES+i]);
                    else if (k == 6)
                        sprintf(filename, "%s_sr_band%d.img", 
                                scene_list[m*MAX_SCENES+i], k+1);
                    else 
                        sprintf(filename, "%s_sr_band%d.img", 
                                scene_list[m*MAX_SCENES+i], k+2);
                }

                fp_bin[i][k] = open_raw_binary(filename,"rb");
                if (fp_bin[i][k] == NULL)
                    printf("error open %d scene, %d bands files\n",i, k+1);
                if (k != TOTAL_BANDS-1)
                {
                    fseek(fp_bin[i][k], (row * meta->samples + col)*sizeof(short int), 
                          SEEK_SET);
                    if (read_raw_binary(fp_bin[i][k], meta->lines, meta->samples, 
                        sizeof(short int), &buf[m*MAX_SCENES+i][k]) != 0)
                        printf("error reading %d scene, %d bands\n",i, k+1);
#if 0
                    printf("scene_number,band_number,buf[i][k] = %d, %d, %d\n",
                       m*MAX_SCENES+i,k+1,buf[m*MAX_SCENES+i][k]);
#endif
                }
                else
                {
                    fseek(fp_bin[i][k], (row * meta->samples + col)*sizeof(unsigned char), 
                        SEEK_SET);
                    if (read_raw_binary(fp_bin[i][k], meta->lines, meta->samples, 
                        sizeof(unsigned char), &fmask_buf[m*MAX_SCENES+i]) != 0)
                        printf("error reading %d scene, %d bands\n",i, k+1);
#if 0
                    printf("scene_number,band_number,buf[i][k] = %d, %d, %d\n",
                           m*MAX_SCENES+i,k+1,fmask_buf[m*MAX_SCENES+i]);
#endif
                }
            close_raw_binary(fp_bin[i][k]);
            }
        }
    }

    for (i = 0; i < num_scenes - loop_number * MAX_SCENES; i++)
    {
        for (k = 0; k < TOTAL_BANDS; k++)
        {
            landsat_number = atoi(sub_string(scene_list[i + loop_number * MAX_SCENES],2,1));
            if (landsat_number != 8)
            {
                if (k == 5)
                    sprintf(filename, "%s_toa_band6.img", 
                            scene_list[i + loop_number * MAX_SCENES]);
                else if (k == 7)
                    sprintf(filename, "%s_cfmask.img", 
                            scene_list[i + loop_number * MAX_SCENES]);
                else
                    sprintf(filename, "%s_sr_band%d.img", 
                            scene_list[i + loop_number * MAX_SCENES], k+1);
            }
            else
            {
                if (k == 5)
                    sprintf(filename, "%s_toa_band10.img", 
                            scene_list[i + loop_number * MAX_SCENES]);
                else if (k == 7)
                    sprintf(filename, "%s_cfmask.img", 
                            scene_list[i + loop_number * MAX_SCENES]);
                else if (k == 6)
                    sprintf(filename, "%s_sr_band%d.img", 
                            scene_list[i + loop_number * MAX_SCENES], k+1);
                else 
                    sprintf(filename, "%s_sr_band%d.img", 
                            scene_list[i + loop_number * MAX_SCENES], k+2);
            }
            fp_bin[i][k] = open_raw_binary(filename,"rb");
            if (fp_bin[i][k] == NULL)
            printf("error open %d scene, %d bands files\n",i, k+1);
            if (k != TOTAL_BANDS-1)
            {
                fp_bin[i][k] = open_raw_binary(filename,"rb");
                if (fp_bin[i][k] == NULL)
                    printf("error open %d scene, %d bands files\n",i, k+1);
                fseek(fp_bin[i][k], (row * meta->samples + col)*sizeof(short int), 
                      SEEK_SET);
                if (read_raw_binary(fp_bin[i][k], meta->lines, meta->samples,          , 
                    sizeof(short int), &buf[loop_number*MAX_SCENES+i][k]) != 0)
                    printf("error reading %d scene, %d bands\n",i, k+1);
#if 0
                printf("scene_number,band_number,buf[i][k] = %d, %d, %d\n",
                    loop_number*MAX_SCENES+i,k+1,buf[loop_number*MAX_SCENES+i][k]);
#enif
            }
            else
            {
                fseek(fp_bin[i][TOTAL_BANDS-1], (row * meta->samples + col)*
                      sizeof(unsigned char), SEEK_SET);
                if (read_raw_binary(fp_bin[i][k], meta->lines, meta->samples,
                    sizeof(unsigned char), &fmask_buf[loop_number*MAX_SCENES+i]) != 0)
                    printf("error reading %d scene, %d bands\n",i, k+1);
#if 0
                printf("scene_number,band_number,buf[i][k] = %d, %d, %d\n",
                       loop_number*MAX_SCENES+i,k+1,fmask_buf[loop_number*MAX_SCENES+i]);
#endif
            }
            close_raw_binary(fp_bin[i][k]);
        }
    }

    /* Only run CCDC for places where more than 50% of images has data */
    for (i = 0; i < num_scenes; i++)
    { 
        if (fmask_buf[i] < 255)
            fmask_sum++;
    }
    if (fmask_sum < (uint) 0.5 * num_scenes)
        RETURN_ERROR ("Not enough clear-sky pisels", FUNC_NAME, EXIT_FAILURE);
    else
        printf("Clear-sky pixel percentage = %f7.2\n", fmask_sum / num_scenes);


    /* pixel value ranges should follow physical rules & based on cfmask
       results to get good clear-sky pixel over both land and water */


#if 0
    /* If the scene is an ascending polar scene (flipped upside down), then
       the solar azimuth needs to be adjusted by 180 degrees.  The scene in
       this case would be north down and the solar azimuth is based on north
       being up clock-wise direction. Flip the south to be up will not change 
       the actual sun location, with the below relations, the solar azimuth
       angle will need add in 180.0 for correct sun location */
    if (input->meta.ul_corner.is_fill &&
        input->meta.lr_corner.is_fill &&
        (input->meta.ul_corner.lat - input->meta.lr_corner.lat) < MINSIGMA)
    {
        /* Keep the original solar azimuth angle */
        sun_azi_temp = input->meta.sun_az;
        input->meta.sun_az += 180.0;
        if ((input->meta.sun_az - 360.0) > MINSIGMA)
            input->meta.sun_az -= 360.0;
        if (verbose)
            printf
                ("  Polar or ascending scene.  Readjusting solar azimuth by "
                 "180 degrees.\n  New value: %f degrees\n",
                 input->meta.sun_az);
    }
#endif

    /* call build_modtran_input to generate tape5 file and commandList */
    status = build_modtran_input (input, &num_points, verbose);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Building MODTRAN input\n", FUNC_NAME, EXIT_FAILURE);
    }

    if (verbose)
    {
        printf ("DEBUG: Number of Points: %d\n", num_points);
    }

// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
    exit (0);
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now

    num_cases = num_points * NUM_ELEVATIONS * 3;
    case_list = (char **) allocate_2d_array (num_cases, MAX_STR_LEN,
                                             sizeof (char));
    if (case_list == NULL)
    {
        RETURN_ERROR ("Allocating case_list memory", FUNC_NAME, EXIT_FAILURE);
    }

    command_list = (char **) allocate_2d_array (num_cases, MAX_STR_LEN,
                                                sizeof (char));
    if (command_list == NULL)
    {
        RETURN_ERROR ("Allocating command_list memory", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* Read case_list from caseList file */
    fd = fopen ("caseList", "r");
    if (fd == NULL)
    {
        RETURN_ERROR ("Opening file: caseList\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Read in the caseList file */
    for (k = 0; k < num_cases; k++)
    {
        fscanf (fd, "%s", case_list[k]);
    }

    /* Close the caseList file */
    status = fclose (fd);
    if (status)
    {
        RETURN_ERROR ("Closing file: caseList\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Read command_list from commandList file */
    fd = fopen ("commandList", "r");
    if (fd == NULL)
    {
        RETURN_ERROR ("Opening file: commandList\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Read in the commandList file */
    for (k = 0; k < num_cases; k++)
    {
        fgets (command_list[k], MAX_STR_LEN, fd);
    }

    /* Close the commandList file */
    status = fclose (fd);
    if (status)
    {
        RETURN_ERROR ("Closing file: commandList\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* perform modtran runs by calling command_list */
    for (i = 0; i < num_cases; i++)
    {
        status = system (command_list[i]);
        if (status != SUCCESS)
        {
            RETURN_ERROR ("executing command_list[i]", FUNC_NAME,
                          EXIT_FAILURE);
        }
    }

    /* PARSING TAPE6 FILES: for each case in caseList (for each modtran run),
       copy program to delete headers and parse wavelength and total radiance
       from tape6 file */
    status = system ("cp $BIN/tape6parser.bash .");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("cp $BIN/tape6parser.bash\n", FUNC_NAME, EXIT_FAILURE);
    }

    for (i = 0; i < num_cases; i++)
    {
        /* Just use $LST_DATA/elim2.sed directly instead of linking it */
        sprintf (command, "./tape6parser.bash %s", case_list[i]);
        status = system (command);
        if (status != SUCCESS)
        {
            RETURN_ERROR ("./tape6parser.bash\n", FUNC_NAME, EXIT_FAILURE);
        }
    }

// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
    exit (0);
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
// TODO TODO TODO - RDD - stopping here for right now
    status = system ("rm tape6parser.bash");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("rm tape6parser.bash\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Free memory allocation */
    status = free_2d_array ((void **) command_list);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Freeing memory: command_list\n", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* Allocate memory for results */
    results = (float **) allocate_2d_array (num_points * NUM_ELEVATIONS, 6,
                                            sizeof (float));
    if (results == NULL)
    {
        RETURN_ERROR ("Allocating results memory", FUNC_NAME, EXIT_FAILURE);
    }

    /* call second_narr to generate parameters for each height and NARR point */
    status =
        second_narr (input, num_points, alb, case_list, results, verbose);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Calling scene_based_list\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Free memory allocation */
    status = free_2d_array ((void **) case_list);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Freeing memory: current_case\n", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* call third_pixels_post to generate parameters for each Landsat pixel */
    status = third_pixels_post (input, num_points, dem_name, emissivity_name,
                                results, verbose);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Calling scene_based_list\n", FUNC_NAME, EXIT_FAILURE);
    }

#if 0
    /* Reassign solar azimuth angle for output purpose if south up north 
       down scene is involved */
    if (input->meta.ul_corner.is_fill &&
        input->meta.lr_corner.is_fill &&
        (input->meta.ul_corner.lat - input->meta.lr_corner.lat) < MINSIGMA)
    {
        input->meta.sun_az = sun_azi_temp;
    }

    /* Open the output file */
    output = OpenOutput (&xml_metadata, input);
    if (output == NULL)
    {                           /* error message already printed */
        RETURN_ERROR ("Opening output file", FUNC_NAME, EXIT_FAILURE);
    }

    if (!PutOutput (output, pixel_mask))
    {
        RETURN_ERROR ("Writing output LST in HDF files\n", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* Close the output file */
    if (!CloseOutput (output))
    {
        RETURN_ERROR ("closing output file", FUNC_NAME, EXIT_FAILURE);
    }

    /* Create the ENVI header file this band */
    if (create_envi_struct (&output->metadata.band[0], &xml_metadata.global,
                            &envi_hdr) != SUCCESS)
    {
        RETURN_ERROR ("Creating ENVI header structure.", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* Write the ENVI header */
    strcpy (envi_file, output->metadata.band[0].file_name);
    cptr = strchr (envi_file, '.');
    if (cptr == NULL)
    {
        RETURN_ERROR ("error in ENVI header filename", FUNC_NAME,
                      EXIT_FAILURE);
    }

    strcpy (cptr, ".hdr");
    if (write_envi_hdr (envi_file, &envi_hdr) != SUCCESS)
    {
        RETURN_ERROR ("Writing ENVI header file.", FUNC_NAME, EXIT_FAILURE);
    }

    /* Append the LST band to the XML file */
    if (append_metadata (output->nband, output->metadata.band, xml_name)
        != SUCCESS)
    {
        RETURN_ERROR ("Appending spectral index bands to XML file.",
                      FUNC_NAME, EXIT_FAILURE);
    }

    /* Free the structure */
    if (!FreeOutput (output))
    {
        RETURN_ERROR ("freeing output file structure", FUNC_NAME,
                      EXIT_FAILURE);
    }

    /* Free the metadata structure */
    free_metadata (&xml_metadata);

    /* Close the input file and free the structure */
    CloseInput (input);
    FreeInput (input);

    free (xml_name);
    printf ("Processing complete.\n");
#endif

    /* Free memory allocation */
    status = free_2d_array ((void **) results);
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Freeing memory: results\n", FUNC_NAME, EXIT_FAILURE);
    }

#if 0
    /* Delete temporary file */
    status = system ("rm newHead*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting newHead* files\n", FUNC_NAME, EXIT_FAILURE);
    }

    status = system ("rm newTail*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting newTail* files\n", FUNC_NAME, EXIT_FAILURE);
    }

    status = system ("rm tempLayers.txt");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting tempLayers file\n", FUNC_NAME, EXIT_FAILURE);
    }

    /* Delete temporary directories */
    status = system ("\rm -r HGT*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting HGT* directories\n", FUNC_NAME, EXIT_FAILURE);
    }

    status = system ("\rm -r SHUM*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting SHUM* directories\n", FUNC_NAME, EXIT_FAILURE);
    }

    status = system ("\rm -r TMP*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting TMP* directories\n", FUNC_NAME, EXIT_FAILURE);
    }

    status = system ("\rm -r 4?.*_*");
    if (status != SUCCESS)
    {
        RETURN_ERROR ("Deleting temporary directories\n", FUNC_NAME,
                      EXIT_FAILURE);
    }
#endif

    time (&now);
    snprintf (msg_str, sizeof(msg_str),
              "CCDC end_time=%s\n", ctime (&now));
    LOG_MESSAGE (msg_str, FUNC_NAME);

    return EXIT_SUCCESS;
}


/******************************************************************************
MODULE:  usage

PURPOSE:  Prints the usage information for this application.

RETURN VALUE:
Type = None

HISTORY:
Date        Programmer       Reason
--------    ---------------  -------------------------------------
3/15/2013   Song Guo         Original Development
8/15/2013   Song Guo         Modified to use TOA reflectance file 
                             as input instead of metadata file
2/19/2014   Gail Schmidt     Modified to utilize the ESPA internal raw binary
                             file format

******************************************************************************/
void
usage ()
{
    printf ("Landsat Surface Temperature\n");
    printf ("\n");
    printf ("usage: scene_based_lst"
            " --xml=input_xml_filename"
            " --dem=input_dem_filename"
            " --emi=input_emissivity_filename" " [--verbose]\n");

    printf ("\n");
    printf ("where the following parameters are required:\n");
    printf ("    -xml: name of the input XML file\n");
    printf ("\n");
    printf ("where the following parameters are optional:\n");
    printf ("    -verbose: should intermediate messages be printed?"
            " (default is false)\n");
    printf ("\n");
    printf ("scene_based_lst --help will print the usage statement\n");
    printf ("\n");
    printf ("Example: scene_based_lst"
            " --xml=LE70390032010263EDC00.xml"
            " --dem=17_30_DEM.tif"
            " --emi=AG100B.v003.-20.122.0001.bin" " --verbose\n");
    printf ("Note: The scene_based_lst must run from the directory"
            " where the input data are located.\n\n");
}
