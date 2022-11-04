import time
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors, pagesizes

def generate_report_document(report_data, path, title):
    doc = SimpleDocTemplate(str(path.absolute() / "report.pdf"), 
                        pagesize=pagesizes.A4,
                        rightMargin=18,leftMargin=18,
                        topMargin=18,bottomMargin=18)
    stylesheet = getSampleStyleSheet()
    normalStyle = stylesheet["Normal"]

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

    if "features" in report_data:
        features = report_data["features"]
        story.append(Paragraph("Features", stylesheet["Heading2"]))
        table = Table(features, hAlign='LEFT')
        table.setStyle(TableStyle([
            ('GRID', (0,0), (-1,-1), 0.25, colors.black),
            ('LEFTPADDING', (0, 1), (-1, -1), 2),
            ('RIGHTPADDING', (0, 1), (-1, -1), 2),
            ('TOPPADDING', (0, 1), (-1, -1), 2),
            ('BOTTOMPADDING', (0, 1), (-1, -1), 2)
        ]))
        story.append(table)
        story.append(PageBreak())

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
    
    # List each test case with results
    for test_cases_name, test_cases_value in report_data.items():
        if isinstance(test_cases_value, dict):
            if "not_supported" in test_cases_value:
                summaryItem = [test_cases_name, Paragraph("Skipped")]
                summaryItem.append(Paragraph(str(test_cases_value["requiredFeatures"])))
                summary.append(summaryItem)
                continue
            story.append(Paragraph(f'<a name={test_cases_name}></a>{test_cases_name}', stylesheet["Heading2"]))
            summaryItem = [Paragraph(f'<a href="#{test_cases_name}" color=blue>{test_cases_name}</a>'), passed]
            if "requiredFeatures" in test_cases_value:
                summaryItem.append(Paragraph(str(test_cases_value["requiredFeatures"])))
                story.extend([
                    Paragraph("Required features:", stylesheet["Normal"]), 
                    Paragraph(f'{test_cases_value["requiredFeatures"]}', stylesheet["Normal"])
                ]
                )

            summary.append(summaryItem)
            for name, nameValue in test_cases_value.items():
                if isinstance(nameValue, dict):
                    status = passed
                    story.append(Paragraph(f'<a name={name}></a>{name}', stylesheet["Heading3"]))
                    if "frameDuration" in nameValue:
                        story.append(
                            Paragraph(f'Frame duration: {nameValue["frameDuration"]:10.5f}', stylesheet["Normal"])
                        )
                        if nameValue["frameDuration"] < 0:
                            status = failed
                            summaryItem[1] = failed
                    if "property_check" in nameValue:
                        story.append(
                            Paragraph(f'Property check: {nameValue["property_check"][0]}', stylesheet["Normal"])
                        )
                        if not nameValue["property_check"][1]:
                            status = failed
                            summaryItem[1] = failed
                    for channel, results in nameValue.items():
                        if isinstance(results, dict):
                            story.append(Paragraph(channel, stylesheet["Heading3"]))
                            # Evaluated metrics
                            cell_size = (doc.width / 3)
                            metrics_data = [
                                [
                                    Paragraph("Metric", stylesheet["Heading4"]),
                                    Paragraph("Value", stylesheet["Heading4"]),
                                    Paragraph("Result", stylesheet["Heading4"])
                                ]
                            ]

                            for metricName, result in results["metrics"].items():
                                metrics_data.append([
                                    Paragraph(metricName.upper(), stylesheet["Normal"]), 
                                    Paragraph(f'<code>{result:10.5f}</code>', stylesheet["Normal"]), 
                                    Paragraph("Above Threshold" if results["passed"][metricName] else '<font color="orange">Below Threshold</font>', stylesheet["Normal"])
                                ])
                                if not results["passed"][metricName]:
                                    status = failed
                                    summaryItem[1] = failed
                            

                            t = Table(metrics_data, 3 * [cell_size])
                            t.setStyle(TableStyle([
                                ('GRID', (0,0), (-1,-1), 0.25, colors.black),
                            ]))
                            story.append(t)
                            story.append(Spacer(1, 12))

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
                            story.append(t)
                            story.append(Spacer(1, 12))

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
                            story.append(t)

                            story.append(PageBreak())
                        
                    summary.append([Paragraph(f'<a href="#{name}" color=blue>&nbsp;&nbsp;&nbsp;&nbsp;{name}</a>'), status])


    table = Table(summary, colWidths=[doc.width / 2.5, doc.width / 8, doc.width / 2.5])
    table.setStyle(TableStyle([
        ('GRID', (0,0), (-1,-1), 0.25, colors.black),
        ('LEFTPADDING', (0, 1), (-1, -1), 2),
        ('RIGHTPADDING', (0, 1), (-1, -1), 2),
        ('TOPPADDING', (0, 1), (-1, -1), 2),
        ('BOTTOMPADDING', (0, 1), (-1, -1), 2)
    ]))
    story.insert(3, table)
            
    doc.build(story)
