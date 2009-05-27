/*------------------------------------------------------------------------------
* Copyright (C) 2003-2006 Ben van Klinken and the CLucene Team
*
* Distributable under the terms of either the Apache License (Version 2.0) or
* the GNU Lesser General Public License, as specified in the COPYING file.
------------------------------------------------------------------------------*/
#include "CLucene/_ApiHeader.h"
#include "MultiReader.h"
#include "_MultiSegmentReader.h"

#include "IndexReader.h"
#include "CLucene/document/Document.h"
#include "Term.h"
#include "Terms.h"
#include "CLucene/util/PriorityQueue.h"
#include "_SegmentHeader.h"
#include "_SegmentMergeInfo.h"
#include "_SegmentMergeQueue.h"

CL_NS_USE(store)
CL_NS_USE(document)
CL_NS_USE(util)
CL_NS_DEF(index)



class MultiReader::Internal: LUCENE_BASE{
public:
  MultiSegmentReader::NormsCacheType normsCache;

  bool* decrefOnClose; //remember which subreaders to decRef on close
  bool _hasDeletions;
  uint8_t* ones;
  int32_t _maxDoc;
  int32_t _numDocs;

	Internal():
  		normsCache(true, true)
	{
    _maxDoc        = 0;
    _numDocs       = -1;
    ones           = NULL;
	}
	~Internal(){
	}
};

MultiReader::MultiReader(CL_NS(util)::ArrayBase<IndexReader*>* subReaders, bool closeSubReaders)
{
	this->_internal = _CLNEW Internal();
  this->init(subReaders, closeSubReaders);
}

void MultiReader::init(CL_NS(util)::ArrayBase<IndexReader*>* _subReaders, bool closeSubReaders){
  this->subReaders = _subReaders;
  starts = _CL_NEWARRAY(int32_t, subReaders->length + 1);    // build starts array
  _internal->decrefOnClose = _CL_NEWARRAY(bool, subReaders->length);

  for (size_t i = 0; i < subReaders->length; i++) {
      starts[i] = _internal->_maxDoc;

      // compute maxDocs
      _internal->_maxDoc += (*subReaders)[i]->maxDoc();

      if (!closeSubReaders) {
        (*subReaders)[i]->incRef();
        _internal->decrefOnClose[i] = true;
      } else {
        _internal->decrefOnClose[i] = false;
      }

      if ((*subReaders)[i]->hasDeletions())
        _internal->_hasDeletions = true;
  }
  starts[subReaders->length] = _internal->_maxDoc;
}

MultiReader::~MultiReader() {
//Func - Destructor
//Pre  - true
//Post - The instance has been destroyed all IndexReader instances
//       this instance managed have been destroyed to

	_CLDELETE(_internal);
  _CLDELETE_ARRAY(starts);
  _CLDELETE_ARRAY(_internal->ones);
  _CLDELETE_ARRAY(_internal->decrefOnClose);
  subReaders->deleteValues();
}


IndexReader* MultiReader::reopen() {
  ensureOpen();

  bool reopened = false;
  ArrayBase<IndexReader*>* newSubReaders = _CLNEW ObjectArray<IndexReader>(subReaders->length);
  bool* newDecrefOnClose = _CL_NEWARRAY(bool,subReaders->length);

  bool success = false;
  try {
    for (size_t i = 0; i < subReaders->length; i++) {
      newSubReaders->values[i] = (*subReaders)[i]->reopen();
      // if at least one of the subreaders was updated we remember that
      // and return a new MultiReader
      if ((*newSubReaders)[i] != (*subReaders)[i]) {
        reopened = true;
        // this is a new subreader instance, so on close() we don't
        // decRef but close it
        newDecrefOnClose[i] = false;
//TODO: cleanup memory
      }
    }

    if (reopened) {
      for (size_t i = 0; i < subReaders->length; i++) {
        if ((*newSubReaders)[i] == (*subReaders)[i]) {
          (*newSubReaders)[i]->incRef();
          newDecrefOnClose[i] = true;
        }
      }

      MultiReader* mr = _CLNEW MultiReader(newSubReaders);
      mr->_internal->decrefOnClose = newDecrefOnClose;
      success = true;
      return mr;
    } else {
      success = true;
      return this;
    }
  } _CLFINALLY (
    if (!success && reopened) {
      for (size_t i = 0; i < newSubReaders->length; i++) {
        if ((*newSubReaders)[i] != NULL) {
          try {
            if (newDecrefOnClose[i]) {
              (*newSubReaders)[i]->decRef();
            } else {
              (*newSubReaders)[i]->close();
            }
          } catch (CLuceneError& ignore) {
            if ( ignore.number() != CL_ERR_IO ) throw ignore;
            // keep going - we want to clean up as much as possible
          }
        }
      }
    }
  )
}

ArrayBase<TermFreqVector*>* MultiReader::getTermFreqVectors(int32_t n){
    ensureOpen();
	int32_t i = readerIndex(n);        // find segment num
	return (*subReaders)[i]->getTermFreqVectors(n - starts[i]); // dispatch to segment
}

TermFreqVector* MultiReader::getTermFreqVector(int32_t n, const TCHAR* field){
    ensureOpen();
	int32_t i = readerIndex(n);        // find segment num
	return (*subReaders)[i]->getTermFreqVector(n - starts[i], field);
}

void MultiReader::getTermFreqVector(int32_t docNumber, const TCHAR* field, TermVectorMapper* mapper) {
  ensureOpen();
  int32_t i = readerIndex(docNumber);        // find segment num
  (*subReaders)[i]->getTermFreqVector(docNumber - starts[i], field, mapper);
}

void MultiReader::getTermFreqVector(int32_t docNumber, TermVectorMapper* mapper) {
  ensureOpen();
  int32_t i = readerIndex(docNumber);        // find segment num
  (*subReaders)[i]->getTermFreqVector(docNumber - starts[i], mapper);
}

bool MultiReader::isOptimized() {
  return false;
}


int32_t MultiReader::numDocs() {
	SCOPED_LOCK_MUTEX(THIS_LOCK)
    // Don't call ensureOpen() here (it could affect performance)
	if (_internal->_numDocs == -1) {			  // check cache
	  int32_t n = 0;				  // cache miss--recompute
	  for (size_t i = 0; i < subReaders->length; i++)
	    n += (*subReaders)[i]->numDocs();		  // sum from readers
	  _internal->_numDocs = n;
	}
	return _internal->_numDocs;
}

int32_t MultiReader::maxDoc() const {
    // Don't call ensureOpen() here (it could affect performance)
	return _internal->_maxDoc;
}

bool MultiReader::document(int32_t n, CL_NS(document)::Document& doc, const FieldSelector* fieldSelector){
	ensureOpen();
  int32_t i = readerIndex(n);			  // find segment num
	return (*subReaders)[i]->document(n - starts[i],doc, fieldSelector);	  // dispatch to segment reader
}

bool MultiReader::isDeleted(const int32_t n) {
    // Don't call ensureOpen() here (it could affect performance)
	int32_t i = readerIndex(n);			  // find segment num
	return (*subReaders)[i]->isDeleted(n - starts[i]);	  // dispatch to segment reader
}

bool MultiReader::hasDeletions() const{
    // Don't call ensureOpen() here (it could affect performance)
    return _internal->_hasDeletions;
}

const ArrayBase<IndexReader*>* MultiReader::getSubReaders() const{
  return subReaders;
}

uint8_t* MultiReader::norms(const TCHAR* field){
	SCOPED_LOCK_MUTEX(THIS_LOCK)
    ensureOpen();
	uint8_t* bytes;
	bytes = _internal->normsCache.get((TCHAR*)field);
	if (bytes != NULL){
	  return bytes;				  // cache hit
	}

	if ( !hasNorms(field) )
		return fakeNorms();

	bytes = _CL_NEWARRAY(uint8_t,maxDoc());
	for (size_t i = 0; i < subReaders->length; i++)
	  (*subReaders)[i]->norms(field, bytes + starts[i]);

	//Unfortunately the data in the normCache can get corrupted, since it's being loaded with string
	//keys that may be deleted while still in use by the map. To prevent this field is duplicated
	//and then stored in the normCache
	TCHAR* key = STRDUP_TtoT(field);
	//update cache
	_internal->normsCache.put(key, bytes);

	return bytes;
}

void MultiReader::norms(const TCHAR* field, uint8_t* result) {
	SCOPED_LOCK_MUTEX(THIS_LOCK)
    ensureOpen();
	uint8_t* bytes = _internal->normsCache.get((TCHAR*)field);
	if (bytes==NULL && !hasNorms(field))
		bytes=fakeNorms();

	if (bytes != NULL){                            // cache hit
	   int32_t len = maxDoc();
	   memcpy(result,bytes,len * sizeof(int32_t));
	}

	for (size_t i = 0; i < subReaders->length; i++)      // read from segments
	  (*subReaders)[i]->norms(field, result + starts[i]);
}


void MultiReader::doSetNorm(int32_t n, const TCHAR* field, uint8_t value){
	_internal->normsCache.removeitr( _internal->normsCache.find((TCHAR*)field) );                         // clear cache
	int32_t i = readerIndex(n);                           // find segment num
	(*subReaders)[i]->setNorm(n-starts[i], field, value); // dispatch
}

TermEnum* MultiReader::terms() {
  ensureOpen();
	return _CLNEW MultiTermEnum(subReaders, starts, NULL);
}

TermEnum* MultiReader::terms(const Term* term) {
    ensureOpen();
	return _CLNEW MultiTermEnum(subReaders, starts, term);
}

int32_t MultiReader::docFreq(const Term* t) {
    ensureOpen();
	int32_t total = 0;				  // sum freqs in Multi
	for (size_t i = 0; i < subReaders->length; i++)
	  total += (*subReaders)[i]->docFreq(t);
	return total;
}

TermDocs* MultiReader::termDocs() {
    ensureOpen();
	TermDocs* ret =  _CLNEW MultiTermDocs(subReaders, starts);
	return ret;
}

TermPositions* MultiReader::termPositions() {
    ensureOpen();
	TermPositions* ret = (TermPositions*)_CLNEW MultiTermPositions(subReaders, starts);
	return ret;
}

void MultiReader::doDelete(const int32_t n) {
	_internal->_numDocs = -1;				  // invalidate cache
	int32_t i = readerIndex(n);			  // find segment num
	(*subReaders)[i]->deleteDocument(n - starts[i]);		  // dispatch to segment reader
	_internal->_hasDeletions = true;
}

int32_t MultiReader::readerIndex(const int32_t n) const {	  // find reader for doc n:
  return MultiSegmentReader::readerIndex(n, this->starts, this->subReaders->length);
}

bool MultiReader::hasNorms(const TCHAR* field) {
    ensureOpen();
	for (size_t i = 0; i < subReaders->length; i++) {
		if ((*subReaders)[i]->hasNorms(field))
			return true;
	}
	return false;
}
uint8_t* MultiReader::fakeNorms() {
	if (_internal->ones==NULL)
		_internal->ones=SegmentReader::createFakeNorms(maxDoc());
	return _internal->ones;
}

void MultiReader::doUndeleteAll(){
	for (size_t i = 0; i < subReaders->length; i++)
		(*subReaders)[i]->undeleteAll();
	_internal->_hasDeletions = false;
	_internal->_numDocs = -1;
}
void MultiReader::doCommit() {
	for (size_t i = 0; i < subReaders->length; i++)
	  (*subReaders)[i]->commit();
}

void MultiReader::doClose() {
	SCOPED_LOCK_MUTEX(THIS_LOCK)
	for (size_t i = 0; i < subReaders->length; i++){
    if (_internal->decrefOnClose[i]) {
        (*subReaders)[i]->decRef();
    } else {
      (*subReaders)[i]->close();
    }
	}
}


void MultiReader::getFieldNames(FieldOption fieldNames, StringArrayWithDeletor& retarray){
    ensureOpen();
    return MultiSegmentReader::getFieldNames(fieldNames, retarray, this->subReaders);
}

bool MultiReader::isCurrent(){
  for (size_t i = 0; i < subReaders->length; i++) {
    if (!(*subReaders)[i]->isCurrent()) {
      return false;
    }
  }

  // all subreaders are up to date
  return true;
}

/** Not implemented.
 * @throws UnsupportedOperationException
 */
int64_t MultiReader::getVersion() {
  _CLTHROWA(CL_ERR_UnsupportedOperation, "MultiReader does not support this method.");
}

const char* MultiReader::getClassName(){
  return "MultiReader";
}
const char* MultiReader::getObjectName() const{
  return getClassName();
}
CL_NS_END
