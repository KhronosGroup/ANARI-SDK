import time
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors, pagesizes

def generate_report_document(report_data, path, title):
    doc = SimpleDocTemplate(str(path.absolute() / "report.pdf"), 
                        pagesize=pagesizes.A4,
                        rightMargin=18,leftMargin=18,
                        topMargin=72,bottomMargin=18)
    stylesheet = getSampleStyleSheet()

    story=[]

    # Header
    story.append(Paragraph(f"{title}",  stylesheet["Heading1"]))
    story.append(Paragraph(f"{time.ctime()}", stylesheet["Normal"]))
    story.append(Spacer(1, 12))

    story.append(PageBreak())

    # List each test case with results
    for report_channels in report_data:
        for name, results in report_channels.items():
            story.append(Paragraph(name, stylesheet["Heading2"]))

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
                
            if "frametime" in results:
                metrics_data.append([
                    Paragraph("Frame duration", stylesheet["Normal"]), 
                    Paragraph(f'<code>{results["frametime"]:10.5f}</code>', stylesheet["Normal"]),
                    Paragraph("-", stylesheet["Normal"])
                ])

            t = Table(metrics_data, 3 * [cell_size])
            t.setStyle(TableStyle([
                ('GRID', (0,0), (-1,-1), 0.25, colors.black),
            ]))
            story.append(t)
            story.append(Spacer(1, 12))

            # Candidate and reference image
            image_size = (doc.width / 2)
            margin = {"left": 6, "right": 6, "top": 6, "bottom": 6}
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
            story.append(Spacer(1, 12))

            story.append(PageBreak())
            
    doc.build(story)
