import time
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors, pagesizes


def generate_report_document(report_data, path, title, check_features = True, verbosity = 0):
    """ Create the pdf report
    Keyword arguments:
    report_data -- json structured data to display
    path -- location of output pdf
    title -- title of the PDF
    verbosity -- 0: only summary 1: show failed tests 2: show all tests
    """
    doc = SimpleDocTemplate(str(path.absolute() / "report.pdf"), 
                        pagesize=pagesizes.A4,
                        rightMargin=18,leftMargin=18,
                        topMargin=18,bottomMargin=18)
    stylesheet = getSampleStyleSheet()
    normalStyle = stylesheet["Normal"]


    # Create passed and failed Paragraph for reusability
    normalStyle.textColor = "green"
    passed = Paragraph("Passed", normalStyle)
    normalStyle.textColor = "red"
    failed = Paragraph("Failed", normalStyle)
    normalStyle.textColor = "black"

    story=[]

    margin = {"left": 6, "right": 6, "top": 6, "bottom": 6}
    summary = []

    # Header
    story.append(Paragraph(f"{title}",  stylesheet["Heading1"]))
    story.append(Paragraph(f"{time.ctime()}", stylesheet["Normal"]))
    story.append(Spacer(1, 12))
    story.append(PageBreak())

    # Extract feature list if present
    if "features" in report_data:
        features = report_data["features"]
        story.append(Paragraph("Features", stylesheet["Heading2"]))
        table = Table(features, hAlign='LEFT')
        table.setStyle(TableStyle([
            ('GRID', (0,0), (-1,-1), 0.25, colors.black)
        ]))
        story.append(table)
        story.append(PageBreak())

    # Extract anariInfo if present
    if "anariInfo" in report_data:
        formattedString = report_data["anariInfo"]
        formattedString = formattedString.replace(' ', '&nbsp;')
        formattedString = formattedString.replace('\n', '<br />')
        oldFont = normalStyle.fontName
        normalStyle.fontName = "Courier"
        story.append(Paragraph("Queried parameter info", stylesheet["Heading2"]))
        story.append(Paragraph(f'<code>{formattedString}</code>', normalStyle))
        normalStyle.fontName = oldFont

    story.append(PageBreak())
    
    # Iterate through each test file
    for test_cases_name, test_cases_value in report_data.items():
        if isinstance(test_cases_value, dict):
            # Add all items to sub stories to be able to not add them depending on verbosity
            test_case_story = []
            # Check if test is supported by this device, otherwise skip
            if "not_supported" in test_cases_value:
                summaryItem = [test_cases_name, Paragraph("Skipped")]
                for i, item in enumerate(test_cases_value["requiredFeatures"]):
                    if item in test_cases_value["not_supported"]:
                        test_cases_value["requiredFeatures"][i] = f'<font color="red">{item}</font>'
                if check_features:
                    summaryItem.append(Paragraph(str(test_cases_value["requiredFeatures"])))
                    summary.append(summaryItem)
                    continue
            # Add heading with ref
            test_case_story.append(Paragraph(f'<a name={test_cases_name}></a>{test_cases_name}', stylesheet["Heading2"]))
            # Create summery item for current test file (this will be changed later)
            summaryItem = [Paragraph(f'<a href="#{test_cases_name}" color=blue>{test_cases_name}</a>'), passed]

            # Show all required features in summary and detailed page
            if "requiredFeatures" in test_cases_value:
                summaryItem.append(Paragraph(str(test_cases_value["requiredFeatures"])))
                test_case_story.extend([
                    Paragraph("Required features:", stylesheet["Normal"]), 
                    Paragraph(f'{test_cases_value["requiredFeatures"]}', stylesheet["Normal"])
                ]
                )
            # Save current count to check if subsections were added later
            oldCount = len(test_case_story)
            summary.append(summaryItem)

            # Iterate through all permutations/variants
            for name, nameValue in test_cases_value.items():
                if isinstance(nameValue, dict):
                    # Add all items to sub stories to be able to not add them depending on verbosity
                    test_story = []
                    status = passed
                    # Add heading with ref
                    test_story.append(Paragraph(f'<a name={name}></a>{name}', stylesheet["Heading3"]))

                    # Check for frameDuration, if below 0 test failed
                    if "frameDuration" in nameValue:
                        test_story.append(
                            Paragraph(f'Frame duration: {nameValue["frameDuration"]:10.5f}', stylesheet["Normal"])
                        )
                        if nameValue["frameDuration"] < 0:
                            status = failed
                            summaryItem[1] = failed

                    # Check for properties
                    if "property_check" in nameValue:
                        test_story.append(
                            Paragraph(f'Property check: {nameValue["property_check"][0]}', stylesheet["Normal"])
                        )
                        if not nameValue["property_check"][1]:
                            status = failed
                            summaryItem[1] = failed

                    # Iterate through all channels (color, depth)
                    for channel, results in nameValue.items():
                        if isinstance(results, dict):
                            # Add all items to sub stories to be able to not add them depending on verbosity
                            channel_story = []
                            channel_story.append(Paragraph(channel, stylesheet["Heading3"]))
                            # Evaluated metrics
                            cell_size = (doc.width / 4)
                            metrics_data = [
                                [
                                    Paragraph("Metric", stylesheet["Heading4"]),
                                    Paragraph("Value", stylesheet["Heading4"]),
                                    Paragraph("Threshold", stylesheet["Heading4"]),
                                    Paragraph("Result", stylesheet["Heading4"])
                                ]
                            ]

                            for metricName, result in results["metrics"].items():
                                metrics_data.append([
                                    Paragraph(metricName.upper(), stylesheet["Normal"]), 
                                    Paragraph(f'<code>{result:10.5f}</code>', stylesheet["Normal"]), 
                                    Paragraph(f'<code>{results["thresholds"][metricName]:10.5f}</code>', stylesheet["Normal"]), 
                                    Paragraph("Above Threshold" if results["passed"][metricName] else '<font color="orange">Below Threshold</font>', stylesheet["Normal"])
                                ])
                                if not results["passed"][metricName]:
                                    status = failed
                                    summaryItem[1] = failed
                            

                            t = Table(metrics_data, 4 * [cell_size])
                            t.setStyle(TableStyle([
                                ('GRID', (0,0), (-1,-1), 0.25, colors.black),
                            ]))
                            channel_story.append(t)                            
                            channel_story.append(Spacer(1, 12))
                            # Candidate and reference image
                            image_size = (doc.width / 2)
                            images_data = [
                                [ 
                                    Paragraph("Reference", stylesheet["Heading4"]), 
                                    Paragraph("Candidate", stylesheet["Heading4"]), 
                                ],
                                [
                                    Image(
                                        path / results["image_paths"]["reference"], 
                                        width=image_size - (margin["left"] + margin["right"]), 
                                        height=image_size - (margin["top"] + margin["bottom"])
                                    ),
                                    Image(
                                        path / results["image_paths"]["candidate"], 
                                        width=image_size - (margin["left"] + margin["right"]), 
                                        height=image_size - (margin["top"] + margin["bottom"])
                                    ),
                                ]
                            ]
                            t = Table(images_data, 2 * [image_size])
                            t.setStyle(TableStyle([
                                ('GRID', (0,0), (-1,-1), 0.25, colors.black),
                                ('LEFTPADDING', (0, 1), (-1, -1), margin["left"]),
                                ('RIGHTPADDING', (0, 1), (-1, -1), margin["right"]),
                                ('TOPPADDING', (0, 1), (-1, -1), margin["top"]),
                                ('BOTTOMPADDING', (0, 1), (-1, -1), margin["bottom"])
                            ]))
                            channel_story.append(t)
                            channel_story.append(Spacer(1, 12))

                            # computed images
                            image_size = (doc.width / 2)
                            images_data = [
                                [ 
                                    Paragraph("Difference", stylesheet["Heading4"]),
                                    Paragraph("5% Threshold", stylesheet["Heading4"]),
                                ],
                                [
                                    Image(
                                        path / results["image_paths"]["diff"], 
                                        width=image_size - (margin["left"] + margin["right"]), 
                                        height=image_size - (margin["top"] + margin["bottom"])
                                    ),
                                    Image(
                                        path / results["image_paths"]["threshold"], 
                                        width=image_size - (margin["left"] + margin["right"]), 
                                        height=image_size - (margin["top"] + margin["bottom"])
                                    ),
                                ]
                            ]
                            t = Table(images_data, 2 * [image_size])
                            t.setStyle(TableStyle([
                                ('GRID', (0,0), (-1,-1), 0.25, colors.black),
                                ('LEFTPADDING', (0, 1), (-1, -1), margin["left"]),
                                ('RIGHTPADDING', (0, 1), (-1, -1), margin["right"]),
                                ('TOPPADDING', (0, 1), (-1, -1), margin["top"]),
                                ('BOTTOMPADDING', (0, 1), (-1, -1), margin["bottom"])
                            ]))
                            channel_story.append(t)

                            channel_story.append(PageBreak())

                            # Add detailed report according to verbosity
                            if verbosity == 2 or (verbosity == 1 and status == failed):
                                test_story.extend(channel_story)

                    # Add Page Break if non exists currently
                    if not isinstance(test_story[-1], PageBreak):
                        test_story.append(PageBreak())
                    
                    # Add detailed report according to verbosity
                    if verbosity == 2 or (verbosity == 1 and status == failed):
                        test_case_story.extend(test_story)
                        # Add test to summary with reference if detailed report is also included
                        summary.append([Paragraph(f'<a href="#{name}" color=blue>&nbsp;&nbsp;&nbsp;&nbsp;{name}</a>'), status])
                    else:
                        # Add test to summary without reference
                        summary.append([Paragraph(f'&nbsp;&nbsp;&nbsp;&nbsp;{name}'), status])

            # Only add the whole detailed test case if there is actual content to show
            if len(test_case_story) != oldCount:
                story.extend(test_case_story)
            else:
                # If not added, change summary item to not include reference
                summaryItem[0] = Paragraph(f'{test_cases_name}')

    # Create Summary table
    table = Table(summary, colWidths=[doc.width / 2.5, doc.width / 8, doc.width / 2.5])
    table.setStyle(TableStyle([
        ('GRID', (0,0), (-1,-1), 0.25, colors.black)
    ]))
    story.insert(3, table)

    # Remove trailing PageBreaks
    if isinstance(story[-1], PageBreak):
        story = story[:-1]
            
    doc.build(story)
