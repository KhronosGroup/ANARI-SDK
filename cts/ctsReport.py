import time
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors, pagesizes
from reportlab.pdfbase import pdfmetrics

def generate_report_document(report_data, path, title):
    doc = SimpleDocTemplate(str(path.absolute() / "report.pdf"), 
                        pagesize=pagesizes.A4,
                        rightMargin=18,leftMargin=18,
                        topMargin=18,bottomMargin=18)
    stylesheet = getSampleStyleSheet()

    story=[]

    margin = {"left": 6, "right": 6, "top": 6, "bottom": 6}

    # Header
    story.append(Paragraph(f"{title}",  stylesheet["Heading1"]))
    story.append(Paragraph(f"{time.ctime()}", stylesheet["Normal"]))
    story.append(Spacer(1, 12))

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
        style = stylesheet["Normal"]
        oldFont = style.fontName
        style.fontName = "Courier"
        story.append(Paragraph("Queried parameter info", stylesheet["Heading2"]))
        story.append(Paragraph(f'<code>{formattedString}</code>', style))
        style.fontName = oldFont

    story.append(PageBreak())

    # List each test case with results
    for test_cases_name, test_cases_value in report_data.items():
        if isinstance(test_cases_value, dict):
            story.append(Paragraph(test_cases_name, stylesheet["Heading2"]))
            if "requiredFeatures" in test_cases_value:
                story.extend([
                    Paragraph("Required features:", stylesheet["Normal"]), 
                    Paragraph(f'{test_cases_value["requiredFeatures"]}', stylesheet["Normal"])
                ]
                )

            for name, nameValue in test_cases_value.items():
                if isinstance(nameValue, dict):
                    story.append(Paragraph(name, stylesheet["Heading3"]))
                    if "frameDuration" in nameValue:
                        story.append(
                            Paragraph(f'Frame duration: {nameValue["frameDuration"]:10.5f}', stylesheet["Normal"])
                        )
                    if "property_check" in nameValue:
                        story.append(
                            Paragraph(f'Property check: {nameValue["property_check"]}', stylesheet["Normal"])
                        )
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

                            for name, result in results["metrics"].items():
                                metrics_data.append([
                                    Paragraph(name.upper(), stylesheet["Normal"]), 
                                    Paragraph(f'<code>{result:10.5f}</code>', stylesheet["Normal"]), 
                                    Paragraph("Above Threshold" if results["passed"][name] else '<font color="orange">Below Threshold</font>', stylesheet["Normal"])
                                ])
                            

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
            
    doc.build(story)
