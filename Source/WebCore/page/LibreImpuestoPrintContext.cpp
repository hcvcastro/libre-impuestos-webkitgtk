/*
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2007 Apple Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* Patched by Henry Castro <hcvcastro@gmail.com> */

#include "config.h"
#include "LibreImpuestoPrintContext.h"

#include "GraphicsContext.h"
#include "Frame.h"
#include "FrameView.h"
#include "RenderLayer.h"
#include "RenderView.h"
#include "HTMLElement.h"
#include "HTMLNames.h"
#include "HTMLTableCaptionElement.h"
#include "HTMLTableCellElement.h"
#include "HTMLTableElement.h"
#include "HTMLTableRowElement.h"
#include "RenderObject.h"
#include "RenderTableCell.h"
#include "RenderTableRow.h"
#include "NodeList.h"
#include <wtf/text/WTFString.h>
#include "PlatformContextCairo.h"

namespace WebCore {

// By imaging to a width a little wider than the available pixels,
// thin pages will be scaled down a little, matching the way they
// print in IE and Camino. This lets them use fewer sheets than they
// would otherwise, which is presumably why other browsers do this.
// Wide pages will be scaled down more than this.
const float printingMinimumShrinkFactor = 1.25f;

// This number determines how small we are willing to reduce the page content
// in order to accommodate the widest line. If the page would have to be
// reduced smaller to make the widest line fit, we just clip instead (this
// behavior matches MacIE and Mozilla, at least)
const float printingMaximumShrinkFactor = 2;

LibreImpuestoPrintContext::LibreImpuestoPrintContext(Frame* header, Frame* frame, Frame* footer)
    : m_contentFrame(frame), m_headerFrame(header), m_footerFrame(footer)
    , m_isPrinting(false)
{
}

LibreImpuestoPrintContext::~LibreImpuestoPrintContext()
{
    if (m_isPrinting)
        end();
}

// Called after begin
void LibreImpuestoPrintContext::computePageRects(const FloatRect& printRect, float headerHeight, float footerHeight, float userScaleFactor, float& outPageHeight, bool allowHorizontalTiling)
{
    RenderView* view;

    m_pageContentRects.clear();
    m_pageHeaderRects.clear();
    outPageHeight = 0;

    if (!m_contentFrame->document() || !m_contentFrame->view() || !m_contentFrame->document()->renderer())
        return;

    if (userScaleFactor <= 0) {
        LOG_ERROR("userScaleFactor has bad value %.2f", userScaleFactor);
        return;
    }

    view = toRenderView(m_contentFrame->document()->renderer());
    const IntRect& documentRect = view->documentRect();
    FloatSize pageSize = m_contentFrame->resizePageRectsKeepingRatio(FloatSize(printRect.width(), printRect.height()), FloatSize(documentRect.width(), documentRect.height()));

    pageSize.setHeight( pageSize.height() - headerHeight - footerHeight );

    float pageWidth = pageSize.width();
    float pageHeight = pageSize.height();

    outPageHeight = pageHeight; // this is the height of the page adjusted by margins

    computeHeaderPageRectsWithPageSizeInternal(FloatSize(pageWidth / userScaleFactor, pageHeight / userScaleFactor), allowHorizontalTiling);

    if (pageHeight <= 0) {
        LOG_ERROR("pageHeight has bad value %.2f", pageHeight);
        return;
    }

    computePageRectsWithPageSizeInternal(FloatSize(pageWidth / userScaleFactor, pageHeight / userScaleFactor), allowHorizontalTiling);

}

void LibreImpuestoPrintContext::computePageRectsWithPageSize(const FloatSize& pageSizeInPixels, bool allowHorizontalTiling)
{
    m_pageContentRects.clear();
    m_pageHeaderRects.clear();
    computePageRectsWithPageSizeInternal(pageSizeInPixels, allowHorizontalTiling);
}


void LibreImpuestoPrintContext::computeHeaderPageRectsWithPageSizeInternal(const FloatSize& pageSizeInPixels, bool allowInlineDirectionTiling)
{
    if (!m_headerFrame->document() || !m_headerFrame->view() || !m_headerFrame->document()->renderer())
        return;

    RenderView* view = toRenderView(m_headerFrame->document()->renderer());

    IntRect docRect = view->documentRect();

    int pageWidth = pageSizeInPixels.width();
    int pageHeight = pageSizeInPixels.height();

    bool isHorizontal = view->style()->isHorizontalWritingMode();

    int docLogicalHeight = isHorizontal ? docRect.height() : docRect.width();
    int pageLogicalHeight = isHorizontal ? pageHeight : pageWidth;
    int pageLogicalWidth = isHorizontal ? pageWidth : pageHeight;

    int inlineDirectionStart;
    int inlineDirectionEnd;
    int blockDirectionStart;
    int blockDirectionEnd;
    if (isHorizontal) {
        if (view->style()->isFlippedBlocksWritingMode()) {
            blockDirectionStart = docRect.maxY();
            blockDirectionEnd = docRect.y();
        } else {
            blockDirectionStart = docRect.y();
            blockDirectionEnd = docRect.maxY();
        }
        inlineDirectionStart = view->style()->isLeftToRightDirection() ? docRect.x() : docRect.maxX();
        inlineDirectionEnd = view->style()->isLeftToRightDirection() ? docRect.maxX() : docRect.x();
    } else {
        if (view->style()->isFlippedBlocksWritingMode()) {
            blockDirectionStart = docRect.maxX();
            blockDirectionEnd = docRect.x();
        } else {
            blockDirectionStart = docRect.x();
            blockDirectionEnd = docRect.maxX();
        }
        inlineDirectionStart = view->style()->isLeftToRightDirection() ? docRect.y() : docRect.maxY();
        inlineDirectionEnd = view->style()->isLeftToRightDirection() ? docRect.maxY() : docRect.y();
    }

    unsigned pageCount = ceilf((float)docLogicalHeight / pageLogicalHeight);
    for (unsigned i = 0; i < pageCount; ++i) {
        int pageLogicalTop = blockDirectionEnd > blockDirectionStart ?
                                blockDirectionStart + i * pageLogicalHeight : 
                                blockDirectionStart - (i + 1) * pageLogicalHeight;
        if (allowInlineDirectionTiling) {
            for (int currentInlinePosition = inlineDirectionStart;
                 inlineDirectionEnd > inlineDirectionStart ? currentInlinePosition < inlineDirectionEnd : currentInlinePosition > inlineDirectionEnd;
                 currentInlinePosition += (inlineDirectionEnd > inlineDirectionStart ? pageLogicalWidth : -pageLogicalWidth)) {
                int pageLogicalLeft = inlineDirectionEnd > inlineDirectionStart ? currentInlinePosition : currentInlinePosition - pageLogicalWidth;
                IntRect pageRect(pageLogicalLeft, pageLogicalTop, pageLogicalWidth, pageLogicalHeight);
                if (!isHorizontal)
                    pageRect = pageRect.transposedRect();
                m_pageHeaderRects.append(pageRect);
            }
        } else {
            int pageLogicalLeft = inlineDirectionEnd > inlineDirectionStart ? inlineDirectionStart : inlineDirectionStart - pageLogicalWidth;
            IntRect pageRect(pageLogicalLeft, pageLogicalTop, pageLogicalWidth, pageLogicalHeight);
            if (!isHorizontal)
                pageRect = pageRect.transposedRect();
            m_pageHeaderRects.append(pageRect);
        }
    }
}


void LibreImpuestoPrintContext::computePageRectsWithPageSizeInternal(const FloatSize& pageSizeInPixels, bool allowInlineDirectionTiling)
{
    if (!m_contentFrame->document() || !m_contentFrame->view() || !m_contentFrame->document()->renderer())
        return;

    RenderView* view = toRenderView(m_contentFrame->document()->renderer());

    IntRect docRect = view->documentRect();

    int pageWidth = pageSizeInPixels.width();
    int pageHeight = pageSizeInPixels.height();

    bool isHorizontal = view->style()->isHorizontalWritingMode();

    int docLogicalHeight = isHorizontal ? docRect.height() : docRect.width();
    int pageLogicalHeight = isHorizontal ? pageHeight : pageWidth;
    int pageLogicalWidth = isHorizontal ? pageWidth : pageHeight;

    int inlineDirectionStart;
    int inlineDirectionEnd;
    int blockDirectionStart;
    int blockDirectionEnd;
    if (isHorizontal) {
        if (view->style()->isFlippedBlocksWritingMode()) {
            blockDirectionStart = docRect.maxY();
            blockDirectionEnd = docRect.y();
        } else {
            blockDirectionStart = docRect.y();
            blockDirectionEnd = docRect.maxY();
        }
        inlineDirectionStart = view->style()->isLeftToRightDirection() ? docRect.x() : docRect.maxX();
        inlineDirectionEnd = view->style()->isLeftToRightDirection() ? docRect.maxX() : docRect.x();
    } else {
        if (view->style()->isFlippedBlocksWritingMode()) {
            blockDirectionStart = docRect.maxX();
            blockDirectionEnd = docRect.x();
        } else {
            blockDirectionStart = docRect.x();
            blockDirectionEnd = docRect.maxX();
        }
        inlineDirectionStart = view->style()->isLeftToRightDirection() ? docRect.y() : docRect.maxY();
        inlineDirectionEnd = view->style()->isLeftToRightDirection() ? docRect.maxY() : docRect.y();
    }

    unsigned pageCount = ceilf((float)docLogicalHeight / pageLogicalHeight);
    for (unsigned i = 0; i < pageCount; ++i) {
        int pageLogicalTop = blockDirectionEnd > blockDirectionStart ?
                                blockDirectionStart + i * pageLogicalHeight : 
                                blockDirectionStart - (i + 1) * pageLogicalHeight;
        if (allowInlineDirectionTiling) {
            for (int currentInlinePosition = inlineDirectionStart;
                 inlineDirectionEnd > inlineDirectionStart ? currentInlinePosition < inlineDirectionEnd : currentInlinePosition > inlineDirectionEnd;
                 currentInlinePosition += (inlineDirectionEnd > inlineDirectionStart ? pageLogicalWidth : -pageLogicalWidth)) {
                int pageLogicalLeft = inlineDirectionEnd > inlineDirectionStart ? currentInlinePosition : currentInlinePosition - pageLogicalWidth;
                IntRect pageRect(pageLogicalLeft, pageLogicalTop, pageLogicalWidth, pageLogicalHeight);
                if (!isHorizontal)
                    pageRect = pageRect.transposedRect();
                m_pageContentRects.append(pageRect);
            }
        } else {
            int pageLogicalLeft = inlineDirectionEnd > inlineDirectionStart ? inlineDirectionStart : inlineDirectionStart - pageLogicalWidth;
            IntRect pageRect(pageLogicalLeft, pageLogicalTop, pageLogicalWidth, pageLogicalHeight);
            if (!isHorizontal)
                pageRect = pageRect.transposedRect();
            m_pageContentRects.append(pageRect);
        }
    }
}

bool LibreImpuestoPrintContext::insertHeader( RefPtr<HTMLTableRowElement>& head, RefPtr<Node>& tableBody, unsigned pageHeight)
{
  bool result = false;
  LayoutPoint coord;
  RefPtr<Node> row;
  ExceptionCode ec = 0;
  RenderObject* renderer;
  RenderTableRow* renderTableRow;
  PassRefPtr<NodeList> listRow =  tableBody->childNodes();	  

  for(unsigned iterRow=0; iterRow < listRow->length(); iterRow++) {
    row = listRow->item(iterRow);
    renderer = row->renderer();
    if ( renderer && renderer->isTableRow() ) {
      renderTableRow = toRenderTableRow(renderer);
      coord = roundedLayoutPoint(renderTableRow->localToAbsolute());
      if ( (coord.y() % pageHeight) == 0 && 
	   !row->isEqualNode(head->toNode()) ) {
	tableBody->insertBefore(head->cloneElementWithChildren(), row.get(), ec);
	result = true;
	break;
      }
    }
  }
  return result;
}

void LibreImpuestoPrintContext::fillRows( float width, float height, float headerHeight, float footerHeight )
{
    RenderView* view;
    view = toRenderView(m_contentFrame->document()->renderer());
    const IntRect& documentRect = view->documentRect();

    FloatSize originalPageSize = FloatSize(width, height);
    FloatSize minLayoutSize = m_contentFrame->resizePageRectsKeepingRatio(originalPageSize, 
									  FloatSize(documentRect.width(), documentRect.height()));

    minLayoutSize.setHeight( minLayoutSize.height() - headerHeight - footerHeight);

    Document* document = m_contentFrame->document();
    HTMLElement* body = document->body();
    RefPtr<HTMLTableRowElement> head;
    RefPtr<Node> tableHead;
    RefPtr<Node> tableBody;
    RefPtr<Node> table;    

    // Iterate all tables
    PassRefPtr<NodeList> listTable = body->getElementsByTagName("table");
    unsigned pageHeight = toRenderView(document->renderer())->pageLogicalHeight();
    for (unsigned iterTable=0; iterTable < listTable->length(); iterTable++) {
      table = listTable->item(iterTable);

      // iterate all head sections
      PassRefPtr<NodeList> listHead = table->getElementsByTagName("thead");
      for(unsigned iterHead=0; iterHead < listHead->length(); iterHead++ ) {
	tableHead = listHead->item(iterHead);

	// iterate all rows
	PassRefPtr<NodeList> listRow = tableHead->childNodes();
	for(unsigned iterRow=0; iterRow < listRow->length(); iterRow++ ) {
	  if (listRow->item(iterRow)->hasTagName(HTMLNames::trTag)) {
	    head = static_cast<HTMLTableRowElement*>(listRow->item(iterRow));
	    break;
	  }
	}
      }

      // if no header found .. continue.
      if ( !head ) continue;

      // iterate all body section
      PassRefPtr<NodeList> listBody = table->getElementsByTagName("tbody");
      for(unsigned iterBody=0; iterBody < listBody->length(); iterBody++) {
	tableBody = listBody->item(iterBody);

	while ( insertHeader( head, tableBody, pageHeight ))
	  m_contentFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);
      }

      head = 0;
    }
}

void LibreImpuestoPrintContext::computeHeaderFooterHeight( float pageHeight,  float &headerHeight, float &footerHeight )
{
  RenderView* view;

  view = toRenderView(m_headerFrame->document()->renderer());
  const IntRect& headerRect = view->documentRect();
  headerHeight = headerRect.height();

  if (headerHeight >= pageHeight )
    headerHeight = 0;

  m_headerHeight = headerHeight;

  view = toRenderView(m_footerFrame->document()->renderer());
  const IntRect& footerRect = view->documentRect();
  footerHeight = footerRect.height();

  if (footerHeight >= pageHeight )
    footerHeight = 0;

  m_footerHeight = footerHeight;
}


void LibreImpuestoPrintContext::begin(float width, float height)
{
    // This function can be called multiple times to adjust printing parameters without going back to screen mode.
    m_isPrinting = true;

    FloatSize originalPageSize = FloatSize(width, height);
    FloatSize minLayoutSize = m_contentFrame->resizePageRectsKeepingRatio(originalPageSize, 
									  FloatSize(width * printingMinimumShrinkFactor, height * printingMinimumShrinkFactor));

    // This changes layout, so callers need to make sure that they don't paint to screen while in printing mode.
    m_contentFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);

    // Header
    minLayoutSize = m_headerFrame->resizePageRectsKeepingRatio(originalPageSize, 
							       FloatSize(width * printingMinimumShrinkFactor, height * printingMinimumShrinkFactor));
    m_headerFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);

    // Footer
    minLayoutSize = m_footerFrame->resizePageRectsKeepingRatio(originalPageSize, 
							       FloatSize(width * printingMinimumShrinkFactor, height * printingMinimumShrinkFactor));
    m_footerFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);


}

void LibreImpuestoPrintContext::beginPage(float width, float height, float headerHeight, float footerHeight)
{
    RenderView* view;

    // This function can be called multiple times to adjust printing parameters without going back to screen mode.
    m_isPrinting = true;

    view = toRenderView(m_contentFrame->document()->renderer());
    const IntRect& documentRect = view->documentRect();

    FloatSize originalPageSize = FloatSize(width, height);
    FloatSize minLayoutSize = m_contentFrame->resizePageRectsKeepingRatio(originalPageSize, 
									  FloatSize(documentRect.width(), documentRect.height()));

    minLayoutSize.setHeight( minLayoutSize.height() - headerHeight - footerHeight);

    // This changes layout, so callers need to make sure that they don't paint to screen while in printing mode.
    m_contentFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);

    // Header
    minLayoutSize = m_headerFrame->resizePageRectsKeepingRatio(originalPageSize, 
							       FloatSize(documentRect.width(), documentRect.height()));
    m_headerFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);

    // Footer
    minLayoutSize = m_footerFrame->resizePageRectsKeepingRatio(originalPageSize, 
							       FloatSize(documentRect.width(), documentRect.height()));
    m_footerFrame->setPrinting(true, minLayoutSize, originalPageSize, printingMaximumShrinkFactor / printingMinimumShrinkFactor, AdjustViewSize);


}



float LibreImpuestoPrintContext::computeAutomaticScaleFactor(const FloatSize& availablePaperSize)
{
    if (!m_contentFrame->view())
        return 1;

    bool useViewWidth = true;
    if (m_contentFrame->document() && m_contentFrame->document()->renderView())
        useViewWidth = m_contentFrame->document()->renderView()->style()->isHorizontalWritingMode();

    float viewLogicalWidth = useViewWidth ? m_contentFrame->view()->contentsWidth() : m_contentFrame->view()->contentsHeight();
    if (viewLogicalWidth < 1)
        return 1;

    float maxShrinkToFitScaleFactor = 1 / printingMaximumShrinkFactor;
    float shrinkToFitScaleFactor = (useViewWidth ? availablePaperSize.width() : availablePaperSize.height()) / viewLogicalWidth;
    return max(maxShrinkToFitScaleFactor, shrinkToFitScaleFactor);
}

void LibreImpuestoPrintContext::spoolPage(GraphicsContext& ctx, int pageNumber, float width)
{
    // FIXME: Not correct for vertical text.
  IntRect pageContentRect = m_pageContentRects[pageNumber];
  float scale = width / pageContentRect.width();

  const IntRect& headerRect = IntRect(0, 0, pageContentRect.width(), m_headerHeight);
  const IntRect& footerRect = IntRect(0, 0, pageContentRect.width(), m_footerHeight);

  ctx.save();
  ctx.scale(FloatSize(scale, scale));
  ctx.translate(-pageContentRect.x(), -pageContentRect.y() + headerRect.height());
  //ctx.translate(-pageContentRect.x(), -pageContentRect.y());
  ctx.clip(pageContentRect);
  m_contentFrame->view()->paintContents(&ctx, pageContentRect);
  ctx.restore();

  ctx.save();
  ctx.scale(FloatSize(scale, scale));
  ctx.translate(0,0);
  ctx.clip(headerRect);
  m_headerFrame->view()->paintContents(&ctx, headerRect);
  ctx.restore();

  ctx.save();
  ctx.scale(FloatSize(scale, scale));
  ctx.translate(0, headerRect.height() + pageContentRect.height());
  ctx.clip(footerRect);
  m_footerFrame->view()->paintContents(&ctx, footerRect);
  ctx.restore();


}

void LibreImpuestoPrintContext::spoolRect(GraphicsContext& ctx, const IntRect& rect)
{
    // FIXME: Not correct for vertical text.
    ctx.save();
    ctx.translate(-rect.x(), -rect.y());
    ctx.clip(rect);
    m_contentFrame->view()->paintContents(&ctx, rect);
    ctx.restore();
}

void LibreImpuestoPrintContext::end()
{
    ASSERT(m_isPrinting);
    m_isPrinting = false;
    m_contentFrame->setPrinting(false, FloatSize(), FloatSize(), 0, AdjustViewSize);
}

static RenderBoxModelObject* enclosingBoxModelObject(RenderObject* object)
{

    while (object && !object->isBoxModelObject())
        object = object->parent();
    if (!object)
        return 0;
    return toRenderBoxModelObject(object);
}

int LibreImpuestoPrintContext::pageNumberForElement(Element* element, const FloatSize& pageSizeInPixels)
{
    // Make sure the element is not freed during the layout.
    RefPtr<Element> elementRef(element);
    element->document()->updateLayout();

    RenderBoxModelObject* box = enclosingBoxModelObject(element->renderer());
    if (!box)
        return -1;

    Frame* frame = element->document()->frame();
    FloatRect pageRect(FloatPoint(0, 0), pageSizeInPixels);
    LibreImpuestoPrintContext printContext(NULL, frame, NULL);
    printContext.begin(pageRect.width(), pageRect.height());
    FloatSize scaledPageSize = pageSizeInPixels;
    scaledPageSize.scale(frame->view()->contentsSize().width() / pageRect.width());
    printContext.computePageRectsWithPageSize(scaledPageSize, false);

    int top = box->pixelSnappedOffsetTop();
    int left = box->pixelSnappedOffsetLeft();
    size_t pageNumber = 0;
    for (; pageNumber < printContext.pageCount(); pageNumber++) {
        const IntRect& page = printContext.pageRect(pageNumber);
        if (page.x() <= left && left < page.maxX() && page.y() <= top && top < page.maxY())
            return pageNumber;
    }
    return -1;
}

String LibreImpuestoPrintContext::pageProperty(Frame* frame, const char* propertyName, int pageNumber)
{
    Document* document = frame->document();
    LibreImpuestoPrintContext printContext(NULL, frame, NULL);
    printContext.begin(800); // Any width is OK here.
    document->updateLayout();
    RefPtr<RenderStyle> style = document->styleForPage(pageNumber);

    // Implement formatters for properties we care about.
    if (!strcmp(propertyName, "margin-left")) {
        if (style->marginLeft().isAuto())
            return String("auto");
        return String::number(style->marginLeft().value());
    }
    if (!strcmp(propertyName, "line-height"))
        return String::number(style->lineHeight().value());
    if (!strcmp(propertyName, "font-size"))
        return String::number(style->fontDescription().computedPixelSize());
    if (!strcmp(propertyName, "font-family"))
        return style->fontDescription().family().family().string();
    if (!strcmp(propertyName, "size"))
        return String::number(style->pageSize().width().value()) + ' ' + String::number(style->pageSize().height().value());

    return String("pageProperty() unimplemented for: ") + propertyName;
}

bool LibreImpuestoPrintContext::isPageBoxVisible(Frame* frame, int pageNumber)
{
    return frame->document()->isPageBoxVisible(pageNumber);
}

String LibreImpuestoPrintContext::pageSizeAndMarginsInPixels(Frame* frame, int pageNumber, int width, int height, int marginTop, int marginRight, int marginBottom, int marginLeft)
{
    IntSize pageSize(width, height);
    frame->document()->pageSizeAndMarginsInPixels(pageNumber, pageSize, marginTop, marginRight, marginBottom, marginLeft);

    return "(" + String::number(pageSize.width()) + ", " + String::number(pageSize.height()) + ") " +
           String::number(marginTop) + ' ' + String::number(marginRight) + ' ' + String::number(marginBottom) + ' ' + String::number(marginLeft);
}

int LibreImpuestoPrintContext::numberOfPages(Frame* frame, const FloatSize& pageSizeInPixels)
{
    frame->document()->updateLayout();

    FloatRect pageRect(FloatPoint(0, 0), pageSizeInPixels);
    LibreImpuestoPrintContext printContext(NULL, frame, NULL);
    printContext.begin(pageRect.width(), pageRect.height());
    // Account for shrink-to-fit.
    FloatSize scaledPageSize = pageSizeInPixels;
    scaledPageSize.scale(frame->view()->contentsSize().width() / pageRect.width());
    printContext.computePageRectsWithPageSize(scaledPageSize, false);
    return printContext.pageCount();
}

void LibreImpuestoPrintContext::spoolAllPagesWithBoundaries(Frame* frame, GraphicsContext& graphicsContext, const FloatSize& pageSizeInPixels)
{
    if (!frame->document() || !frame->view() || !frame->document()->renderer())
        return;

    frame->document()->updateLayout();

    LibreImpuestoPrintContext printContext(NULL, frame, NULL);
    printContext.begin(pageSizeInPixels.width(), pageSizeInPixels.height());

    float pageHeight;
    printContext.computePageRects(FloatRect(FloatPoint(0, 0), pageSizeInPixels), 0, 0, 1, pageHeight);

    const float pageWidth = pageSizeInPixels.width();
    const Vector<IntRect>& pageRects = printContext.pageRects();
    int totalHeight = pageRects.size() * (pageSizeInPixels.height() + 1) - 1;

    // Fill the whole background by white.
    graphicsContext.setFillColor(Color(255, 255, 255), ColorSpaceDeviceRGB);
    graphicsContext.fillRect(FloatRect(0, 0, pageWidth, totalHeight));

    graphicsContext.save();
    graphicsContext.translate(0, totalHeight);
    graphicsContext.scale(FloatSize(1, -1));

    int currentHeight = 0;
    for (size_t pageIndex = 0; pageIndex < pageRects.size(); pageIndex++) {
        // Draw a line for a page boundary if this isn't the first page.
        if (pageIndex > 0) {
            graphicsContext.save();
            graphicsContext.setStrokeColor(Color(0, 0, 255), ColorSpaceDeviceRGB);
            graphicsContext.setFillColor(Color(0, 0, 255), ColorSpaceDeviceRGB);
            graphicsContext.drawLine(IntPoint(0, currentHeight),
                                     IntPoint(pageWidth, currentHeight));
            graphicsContext.restore();
        }

        graphicsContext.save();
        graphicsContext.translate(0, currentHeight);
        printContext.spoolPage(graphicsContext, pageIndex, pageWidth);
        graphicsContext.restore();

        currentHeight += pageSizeInPixels.height() + 1;
    }

    graphicsContext.restore();
}

}
